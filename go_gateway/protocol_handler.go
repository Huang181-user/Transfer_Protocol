package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#cgo LDFLAGS: -L../build -lzhiauth_core -lstdc++ -lsqlite3 -lsodium
#include "auth_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"bytes"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"io"
	"log"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/quic-go/quic-go"
)

type UserContext struct {
	Username   string
	SharedPath string
}

var sessionCache sync.Map
var lastAuthTime sync.Map

func HandleIncomingStream(stream *quic.Stream, port string, conn *quic.Conn) {
	defer stream.Close()
	defer stream.CancelRead(0)

	data, err := io.ReadAll(stream)
	if err != nil && err != io.EOF {
		return
	}

	remoteIP := (*conn).RemoteAddr().(*net.UDPAddr).IP.String()

	// Cập nhật nhịp tim ngay khi có bất kỳ dấu hiệu sống nào từ Client!
	lastAuthTime.Store(remoteIP, time.Now())

	authPortStr := strconv.Itoa(globalConfig.Network.AuthPort)
	quicDataPortStr := strconv.Itoa(globalConfig.Network.QuicDataPort)

	if port == authPortStr && string(data) == "AUTH_REQ|PING" {
		stream.Write([]byte("PONG"))
		return
	}

	if port == authPortStr && bytes.HasPrefix(data, []byte("AUTH_REQ|")) {
		handleSecurityAuthentication(stream, string(data), remoteIP)
	} else if port == quicDataPortStr {
		ctxVal, exists := sessionCache.Load(remoteIP)
		if !exists {
			return
		}

		userCtx := ctxVal.(*UserContext)
		udsPath := "/tmp/zhiauth_kcp_" + userCtx.Username + ".sock"
		connUDS, err := net.Dial("unix", udsPath)
		if err != nil {
			return
		}
		defer connUDS.Close()

		connUDS.SetDeadline(time.Now().Add(15 * time.Second))

		reqSz := uint32(len(data))
		binary.Write(connUDS, binary.LittleEndian, reqSz)
		connUDS.Write(data)

		var respSz uint32
		if err := binary.Read(connUDS, binary.LittleEndian, &respSz); err == nil {
			respData := make([]byte, respSz)
			io.ReadFull(connUDS, respData)
			stream.Write(respData)
		}
	}
}

func handleSecurityAuthentication(stream *quic.Stream, payload string, remoteIP string) {
	parts := strings.Split(payload, "|")
	var user, pass, lan, ts, hwid string
	for _, part := range parts {
		if strings.HasPrefix(part, "USER:") { user = strings.TrimPrefix(part, "USER:") }
		if strings.HasPrefix(part, "PASS:") { pass = strings.TrimPrefix(part, "PASS:") }
		if strings.HasPrefix(part, "LAN:")  { lan = strings.TrimPrefix(part, "LAN:") }
		if strings.HasPrefix(part, "TS:")   { ts = strings.TrimPrefix(part, "TS:") }
		if strings.HasPrefix(part, "HWID:") { hwid = strings.TrimPrefix(part, "HWID:") }
	}

	// 🎯 SỬ DỤNG MUỐI BẢO MẬT ĐỘNG TỪ CONFIG
	hash := sha256.New()
	hash.Write([]byte(pass + globalConfig.Security.HashSalt))
	hashedPass := hex.EncodeToString(hash.Sum(nil))

	cUser, cPass, cLan, cTs, cHwid := C.CString(user), C.CString(hashedPass), C.CString(lan), C.CString(ts), C.CString(hwid)
	defer C.free(unsafe.Pointer(cUser))
	defer C.free(unsafe.Pointer(cPass))
	defer C.free(unsafe.Pointer(cLan))
	defer C.free(unsafe.Pointer(cTs))
	defer C.free(unsafe.Pointer(cHwid))

	if ctxVal, exists := sessionCache.Load(lan); exists {
		oldCtx := ctxVal.(*UserContext)
		exec.Command("sudo", "pkill", "-9", "-u", oldCtx.Username, "-f", "zhiauth_kcp_worker").Run()

		cOldIP, cOldPath := C.CString(lan), C.CString(oldCtx.SharedPath)
		C.zhiauth_revoke_access(cOldIP, cOldPath)
		C.free(unsafe.Pointer(cOldIP))
		C.free(unsafe.Pointer(cOldPath))
	}

	resultCStr := C.zhiauth_authenticate_and_trigger(cUser, cPass, cLan, cTs, cHwid)
	resultStr := C.GoString(resultCStr)

	if strings.HasPrefix(resultStr, "1|") {
		resParts := strings.Split(resultStr, "|")
		if len(resParts) >= 3 {
			dbUser, dbPath := resParts[1], resParts[2]

			udsPath := "/tmp/zhiauth_kcp_" + dbUser + ".sock"
			os.Remove(udsPath)
			kcpWorkerCmd := exec.Command("sudo", "-n", "-u", dbUser, "/usr/local/bin/zhiauth_kcp_worker", udsPath)
			kcpWorkerCmd.Start()

			newCtx := &UserContext{Username: dbUser, SharedPath: dbPath}
			sessionCache.Store(lan, newCtx)
			sessionCache.Store(ts, newCtx)
			sessionCache.Store(remoteIP, newCtx)
			lastAuthTime.Store(remoteIP, time.Now())
			lastAuthTime.Store(lan, time.Now())
			lastAuthTime.Store(ts, time.Now())

			log.Printf("[AUTH-SUCCESS] Phê duyệt IP: %s | User: %s", remoteIP, dbUser)
			stream.Write([]byte("AUTH_SUCCESS|" + dbPath))
		}
	} else {
		log.Printf("[AUTH-FAILED] Từ chối xác thực IP: %s", remoteIP)
		stream.Write([]byte("AUTH_FAILED"))
	}
}

// 🎯 HÀM HỦY DIỆT CHÍNH THỨC
func ExecuteSessionKill(ip string) {
	ctxVal, exists := sessionCache.Load(ip)
	if !exists { return }
	userCtx := ctxVal.(*UserContext)

	exec.Command("sudo", "pkill", "-9", "-u", userCtx.Username, "-f", "zhiauth_kcp_worker").Run()

	cIP := C.CString(ip)
	cPath := C.CString(userCtx.SharedPath)
	defer C.free(unsafe.Pointer(cIP))
	defer C.free(unsafe.Pointer(cPath))

	C.zhiauth_revoke_access(cIP, cPath)
	sessionCache.Delete(ip)
	log.Printf("[WATCHDOG-EXEC] 💀 Đã tiêu diệt KCP và thu hồi đặc quyền tường lửa của IP: %s", ip)
}

func TriggerServerSessionCleanup(remoteIP string) {
	go func(ip string) {
		log.Printf("[SESSION-WATCHDOG] Luồng mạng từ %s vừa đóng. Đang theo dõi 20s xem có kết nối lại không...", ip)
		time.Sleep(20 * time.Second)

		if authTimeVal, ok := lastAuthTime.Load(ip); ok {
			lastActivity := authTimeVal.(time.Time)
			if time.Since(lastActivity) < 20*time.Second {
				log.Printf("[SESSION-SAVED] 🛡️ IP %s vẫn đang bơm nhịp tim. Hủy lệnh thảm sát Session!", ip)
				return
			}
		}

		ExecuteSessionKill(ip)
		log.Printf("[SERVER-CLEANUP-SUCCESS] Đã thu hồi toàn bộ đặc quyền mạng đối với IP: %s", ip)
	}(remoteIP)
}

// 🐕 GLOBAL WATCHDOG: TUẦN TRA ĐỊNH KỲ
func StartGlobalWatchdog() {
	log.Println("[WATCHDOG] 🐕 Chó canh gác hệ thống đã thức giấc (Tuần tra mỗi 30s)")
	go func() {
		for {
			time.Sleep(30 * time.Second)
			now := time.Now()
			lastAuthTime.Range(func(key, value interface{}) bool {
				ip := key.(string)
				lastAct := value.(time.Time)
				// Nếu mất liên lạc quá 45 giây -> Hạ sát!
				if now.Sub(lastAct) > 45*time.Second {
					log.Printf("[WATCHDOG-ALERT] 🚨 Mất tín hiệu nhịp tim từ IP %s quá 45s. Tiến hành càn quét!", ip)
					ExecuteSessionKill(ip)
					lastAuthTime.Delete(ip)
				}
				return true
			})
		}
	}()
}
