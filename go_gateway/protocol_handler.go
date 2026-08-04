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
	"fmt"
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

// 🔥 GẮN LẠI DẤU CON TRỎ (*) CHO CHUẨN VỚI QUIC-GO
func HandleIncomingStream(stream *quic.Stream, port string, conn *quic.Conn) {
	defer (*stream).Close()
	defer (*stream).CancelRead(0)

	// Stream vốn dĩ là con trỏ, nên truyền thẳng vào ReadAll
	data, err := io.ReadAll(stream)
	if err != nil && err != io.EOF {
		return
	}

	remoteIP := (*conn).RemoteAddr().(*net.UDPAddr).IP.String()

	if ctxVal, exists := sessionCache.Load(remoteIP); exists {
		userCtx := ctxVal.(*UserContext)
		atomic.StoreInt64(&userCtx.LastActive, time.Now().Unix())
	}

	authPortStr := strconv.Itoa(globalConfig.Network.AuthPort)
	quicDataPortStr := strconv.Itoa(globalConfig.Network.QuicDataPort)

	if port == authPortStr && string(data) == "AUTH_REQ|PING" {
		(*stream).Write([]byte("PONG"))
		return
	}

	if port == authPortStr && bytes.HasPrefix(data, []byte("AUTH_REQ|")) {
		handleSecurityAuthentication(stream, string(data), remoteIP)
	} else if port == quicDataPortStr {
		ctxVal, exists := sessionCache.Load(remoteIP)
		if !exists {
			log.Printf("[SECURITY-ALERT] Ngắt luồng VFS trái phép từ IP: %s", remoteIP)
			(*stream).Write([]byte("FS_ERR|EACCES"))
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
			(*stream).Write(respData)
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

	resultCStr := C.zhiauth_authenticate_and_trigger(cUser, cPass, cLan, cTs, cHwid)
	resultStr := C.GoString(resultCStr)

        if strings.HasPrefix(resultStr, "1|") {
		resParts := strings.Split(resultStr, "|")
		if len(resParts) >= 3 {
			dbUser, dbPath := resParts[1], resParts[2]

			udsPath := "/tmp/zhiauth_kcp_" + dbUser + ".sock"
			// 🔥 MULTI-CLIENT: Khám xét xem Worker đã có mặt chưa, nếu có thì xài chung!
			connTest, errTest := net.DialTimeout("unix", udsPath, 500*time.Millisecond)
			if errTest == nil {
				connTest.Close()
				log.Printf("[%s] [MULTI-CLIENT] 🤝 Worker cho user '%s' đã khởi chạy. Cho phép Client IP %s dùng chung tiến trình!", time.Now().Format("2006-01-02 15:04:05.000"), dbUser, remoteIP)
			} else {
				os.Remove(udsPath)
				kcpWorkerCmd := exec.Command("sudo", "-n", "-u", dbUser, "/usr/local/bin/zhiauth_kcp_worker", udsPath)
				
				// 🔥 FIX CỰC MẠNH: NỐI ỐNG XẢ LOG CỦA TIẾN TRÌNH CON VÀO FILE LOG CHUNG!
				logFile, errLog := os.OpenFile(globalConfig.Paths.LogPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
				if errLog == nil {
					kcpWorkerCmd.Stdout = logFile
					kcpWorkerCmd.Stderr = logFile
				}

				if err := kcpWorkerCmd.Start(); err != nil {
					log.Printf("[%s] [KCP-WORKER-FAIL] ❌ Lỗi gọi tiến trình con: %v", time.Now().Format("2006-01-02 15:04:05.000"), err)
					(*stream).Write([]byte("AUTH_FAILED"))
					if logFile != nil { logFile.Close() }
					return
				}
				
				// Đóng file fd ở tiến trình mẹ để tránh rò rỉ RAM (Tiến trình con đã giữ bản sao)
				if logFile != nil { logFile.Close() }
				
				log.Printf("[%s] [MULTI-CLIENT] 🚀 Đã spawn KCP Worker tiên phong cho user '%s'", time.Now().Format("2006-01-02 15:04:05.000"), dbUser)
			}

			newCtx := &UserContext{
				Username:   dbUser,
				SharedPath: dbPath,
				LanIP:      lan,
				TsIP:       ts,
				LastActive: time.Now().Unix(),
			}			sessionCache.Store(lan, newCtx)
			sessionCache.Store(ts, newCtx)
			sessionCache.Store(remoteIP, newCtx)

			// 🔥 CẬP NHẬT: Nhét Port QUIC và KCP vào gói tin trả về
			log.Printf("[%s] [AUTH-SUCCESS] Phê duyệt IP: %s | User: %s | Cấp phát Port Động: QUIC=%d, KCP=%d", time.Now().Format("2006-01-02 15:04:05.000"), remoteIP, dbUser, globalConfig.Network.QuicDataPort, globalConfig.Network.KcpDataPort)
			authResp := fmt.Sprintf("AUTH_SUCCESS|%s|%d|%d", dbPath, globalConfig.Network.QuicDataPort, globalConfig.Network.KcpDataPort)
			(*stream).Write([]byte(authResp))
		}
	} else {
		log.Printf("[%s] [AUTH-FAILED] Từ chối xác thực IP: %s", time.Now().Format("2006-01-02 15:04:05.000"), remoteIP)
		(*stream).Write([]byte("AUTH_FAILED"))
	}
}

func ExecuteSessionKill(username string, sharedPath string) {
	exec.Command("sudo", "pkill", "-9", "-u", username, "-f", "zhiauth_kcp_worker").Run()
	os.Remove("/tmp/zhiauth_kcp_" + username + ".sock")

	cPath := C.CString(sharedPath)
	defer C.free(unsafe.Pointer(cPath))

	sessionCache.Range(func(key, value interface{}) bool {
		userCtx := value.(*UserContext)
		if userCtx.Username == username {
			if userCtx.LanIP != "" && userCtx.LanIP != "NONE" {
				cLan := C.CString(userCtx.LanIP)
				C.zhiauth_revoke_access(cLan, cPath)
				C.free(unsafe.Pointer(cLan))
			}
			if userCtx.TsIP != "" && userCtx.TsIP != "NONE" {
				cTs := C.CString(userCtx.TsIP)
				C.zhiauth_revoke_access(cTs, cPath)
				C.free(unsafe.Pointer(cTs))
			}
			sessionCache.Delete(key)
		}
		return true
	})
	log.Printf("[WATCHDOG-EXEC] 💀 Đã tiêu diệt KCP Worker và BÍT KÍN UFW đối với toàn bộ truy cập của user: %s", username)
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
			
			userMaxActive := make(map[string]int64)
			userPaths := make(map[string]string)
			
			sessionCache.Range(func(key, value interface{}) bool {
				userCtx := value.(*UserContext)
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

				if currentMax, exists := userMaxActive[userCtx.Username]; !exists || latestActivity > currentMax {
					userMaxActive[userCtx.Username] = latestActivity
					userPaths[userCtx.Username] = userCtx.SharedPath
				}
				return true
			})
			
			for username, latestAct := range userMaxActive {
				if now - latestAct > 120 {
					log.Printf("[WATCHDOG-ALERT] 🚨 Mất tín hiệu TOÀN BỘ Clients của User '%s'. Tiến hành càn quét!", username)
					ExecuteSessionKill(username, userPaths[username])
				}
			}
		}
	}()
}
