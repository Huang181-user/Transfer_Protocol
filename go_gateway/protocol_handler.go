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
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/quic-go/quic-go"
)

type UserContext struct {
	Username   string
	SharedPath string
	LanIP      string 
	TsIP       string 
	LastActive int64 
}

var sessionCache sync.Map

func HandleIncomingStream(stream *quic.Stream, port string, conn *quic.Conn) {
	// Sửa (*stream) thành stream nguyên gốc!
	defer stream.Close()
	defer stream.CancelRead(0)

	data, err := io.ReadAll(stream)
	if err != nil && err != io.EOF {
		return
	}

	remoteIP := conn.RemoteAddr().(*net.UDPAddr).IP.String()

	if ctxVal, exists := sessionCache.Load(remoteIP); exists {
		userCtx := ctxVal.(*UserContext)
		atomic.StoreInt64(&userCtx.LastActive, time.Now().Unix())
	}

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
			log.Printf("[SECURITY-ALERT] Ngắt luồng VFS trái phép từ IP: %s", remoteIP)
			stream.Write([]byte("FS_ERR|EACCES"))
			return
		}

		userCtx := ctxVal.(*UserContext)

		if bytes.HasPrefix(data, []byte("FS_CMD|")) {
			data = data[7:]
		}
		
		udsPath := "/tmp/zhiauth_kcp_" + userCtx.Username + ".sock"
		connUDS, err := net.Dial("unix", udsPath)
		if err != nil {
			log.Printf("[UDS-ERROR] Không thể kết nối tới Socket của Worker: %v", err)
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

	hash := sha256.New()
	hash.Write([]byte(pass + globalConfig.Security.HashSalt))
	hashedPass := hex.EncodeToString(hash.Sum(nil))

	cUser, cPass, cLan, cTs, cHwid := C.CString(user), C.CString(hashedPass), C.CString(lan), C.CString(ts), C.CString(hwid)
	defer C.free(unsafe.Pointer(cUser))
	defer C.free(unsafe.Pointer(cPass))
	defer C.free(unsafe.Pointer(cLan))
	defer C.free(unsafe.Pointer(cTs))
	defer C.free(unsafe.Pointer(cHwid))

	isReconnect := false
	if ctxVal, exists := sessionCache.Load(remoteIP); exists {
		oldCtx := ctxVal.(*UserContext)
		if oldCtx.Username == user { 
			isReconnect = true
			atomic.StoreInt64(&oldCtx.LastActive, time.Now().Unix())
			log.Printf("[AUTH-RENEW] Hầm KCP vắt kiệt băng thông làm rớt QUIC. Đã cứu nạn thành công đường truyền QUIC cho User %s!", user)
		}
	}

	if !isReconnect {
		if ctxVal, exists := sessionCache.Load(lan); exists {
			oldCtx := ctxVal.(*UserContext)
			exec.Command("sudo", "pkill", "-9", "-u", oldCtx.Username, "-f", "zhiauth_kcp_worker").Run()

			cOldPath := C.CString(oldCtx.SharedPath)
			defer C.free(unsafe.Pointer(cOldPath))

			if oldCtx.LanIP != "" && oldCtx.LanIP != "NONE" {
				cOldLan := C.CString(oldCtx.LanIP)
				C.zhiauth_revoke_access(cOldLan, cOldPath)
				C.free(unsafe.Pointer(cOldLan))
			}
			if oldCtx.TsIP != "" && oldCtx.TsIP != "NONE" {
				cOldTs := C.CString(oldCtx.TsIP)
				C.zhiauth_revoke_access(cOldTs, cOldPath)
				C.free(unsafe.Pointer(cOldTs))
			}
		}
	}

	resultCStr := C.zhiauth_authenticate_and_trigger(cUser, cPass, cLan, cTs, cHwid)
	resultStr := C.GoString(resultCStr)

	if strings.HasPrefix(resultStr, "1|") {
		resParts := strings.Split(resultStr, "|")
		if len(resParts) >= 3 {
			dbUser, dbPath := resParts[1], resParts[2]

			if isReconnect {
				stream.Write([]byte("AUTH_SUCCESS|" + dbPath))
				return
			}

			udsPath := "/tmp/zhiauth_kcp_" + dbUser + ".sock"
			os.Remove(udsPath)
			kcpWorkerCmd := exec.Command("sudo", "-n", "-u", dbUser, "/usr/local/bin/zhiauth_kcp_worker", udsPath)
			if err := kcpWorkerCmd.Start(); err != nil {
				log.Printf("[KCP-WORKER-FAIL] ❌ Lỗi gọi tiến trình con: %v", err)
				stream.Write([]byte("AUTH_FAILED"))
				return
			}

			newCtx := &UserContext{
				Username:   dbUser,
				SharedPath: dbPath,
				LanIP:      lan,
				TsIP:       ts,
				LastActive: time.Now().Unix(),
			}
			sessionCache.Store(lan, newCtx)
			sessionCache.Store(ts, newCtx)
			sessionCache.Store(remoteIP, newCtx)

			log.Printf("[AUTH-SUCCESS] Phê duyệt IP: %s | User: %s", remoteIP, dbUser)
			stream.Write([]byte("AUTH_SUCCESS|" + dbPath))
		}
	} else {
		log.Printf("[AUTH-FAILED] Từ chối xác thực IP: %s", remoteIP)
		stream.Write([]byte("AUTH_FAILED"))
	}
}

func ExecuteSessionKill(userCtx *UserContext) {
	exec.Command("sudo", "pkill", "-9", "-u", userCtx.Username, "-f", "zhiauth_kcp_worker").Run()
	os.Remove("/tmp/zhiauth_kcp_" + userCtx.Username + ".sock")

	cPath := C.CString(userCtx.SharedPath)
	defer C.free(unsafe.Pointer(cPath))

	if userCtx.LanIP != "" && userCtx.LanIP != "NONE" {
		cLan := C.CString(userCtx.LanIP)
		C.zhiauth_revoke_access(cLan, cPath)
		C.free(unsafe.Pointer(cLan))
		sessionCache.Delete(userCtx.LanIP)
	}
	if userCtx.TsIP != "" && userCtx.TsIP != "NONE" {
		cTs := C.CString(userCtx.TsIP)
		C.zhiauth_revoke_access(cTs, cPath)
		C.free(unsafe.Pointer(cTs))
		sessionCache.Delete(userCtx.TsIP)
	}
	
	sessionCache.Range(func(key, value interface{}) bool {
		if value.(*UserContext) == userCtx {
			sessionCache.Delete(key)
		}
		return true
	})
	log.Printf("[WATCHDOG-EXEC] 💀 Đã tiêu diệt KCP và BÍT KÍN UFW đối với user: %s", userCtx.Username)
}

func TriggerServerSessionCleanup(remoteIP string) {
	log.Printf("[SESSION-WATCHDOG] 📡 Luồng mạng QUIC từ %s vừa đóng. Mọi quyền phán xét sinh tử giao lại cho Global Watchdog...", remoteIP)
}

func StartGlobalWatchdog() {
	log.Println("[WATCHDOG] 🐕 Chó canh gác hệ thống đã thức giấc (Tuần tra mỗi 30s)")
	go func() {
		for {
			time.Sleep(30 * time.Second)
			now := time.Now().Unix()
			
			checked := make(map[*UserContext]bool)
			
			sessionCache.Range(func(key, value interface{}) bool {
				userCtx := value.(*UserContext)
				if checked[userCtx] { return true }
				checked[userCtx] = true
				
				lastActQUIC := atomic.LoadInt64(&userCtx.LastActive)
				
				cLan := C.CString(userCtx.LanIP)
				cTs := C.CString(userCtx.TsIP)
				lastActKCPLan := int64(C.zhiauth_check_kcp_active(cLan))
				lastActKCPTs := int64(C.zhiauth_check_kcp_active(cTs))
				C.free(unsafe.Pointer(cLan))
				C.free(unsafe.Pointer(cTs))

				lastActKCP := lastActKCPLan
				if lastActKCPTs > lastActKCP { lastActKCP = lastActKCPTs }

				latestActivity := lastActQUIC
				if lastActKCP > latestActivity { latestActivity = lastActKCP }

				if now - latestActivity > 120 {
					log.Printf("[WATCHDOG-ALERT] 🚨 Mất hoàn toàn tín hiệu từ User %s (Cả QUIC lẫn KCP). Tiến hành càn quét!", userCtx.Username)
					ExecuteSessionKill(userCtx)
				}
				return true
			})
		}
	}()
}
