package quicclient

/*
#cgo LDFLAGS: -llog
#include <android/log.h>
#include <stdlib.h>

static void nativeLog(const char* msg) {
    __android_log_write(ANDROID_LOG_DEBUG, "GoLog", msg);
}
*/
import "C"
import (
	"bytes"
	"context"
	"crypto/tls"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"path"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"
    "crypto/rand"

	"github.com/quic-go/quic-go"
)

var (
	session            *quic.Conn
	sessionUdp         *net.UDPConn
	mu                 sync.Mutex
	globalReqId        uint64
	remoteRoot         string
	isHeartbeatRunning bool

	gActiveIp     string
	gAuthPort     string
	gAuthCmd      string
	gQuicDataPort string
	gTlsConf      *tls.Config
	gConfig       *quic.Config
)

func alog(format string, args ...interface{}) {
	now := time.Now().Format("15:04:05.000")
	msg := fmt.Sprintf(format, args...)
	cstr := C.CString(fmt.Sprintf("[HUANG_GO_CORE] [%s] %s", now, msg))
	C.nativeLog(cstr)
	C.free(unsafe.Pointer(cstr))
}

func ReconnectSilently() error {
	alog("🚨 [QUIC-TUNNEL] Kích hoạt luồng kết nối QUIC ngầm để phục hồi mạng...")

	authConn, authUdp, err := dialQuic(context.Background(), "0.0.0.0", gActiveIp, gAuthPort, gTlsConf, gConfig)
	if err != nil { return err }

	authStream, err := (*authConn).OpenStreamSync(context.Background())
	if err != nil { (*authConn).CloseWithError(0, ""); authUdp.Close(); return err }

	authStream.Write([]byte(gAuthCmd))
	authStream.Close()

	res, _ := io.ReadAll(authStream)
	(*authConn).CloseWithError(0, ""); authUdp.Close()

	resStr := string(res)
	if !strings.HasPrefix(resStr, "AUTH_SUCCESS") { return fmt.Errorf("re-auth failure") }

	time.Sleep(200 * time.Millisecond)

	dataConn, dataUdp, err := dialQuic(context.Background(), "0.0.0.0", gActiveIp, gQuicDataPort, gTlsConf, gConfig)
	if err != nil { return err }

	if session != nil { (*session).CloseWithError(0, "") }
	if sessionUdp != nil { sessionUdp.Close() }

	session = dataConn
	sessionUdp = dataUdp

	alog("✅ [QUIC-TUNNEL] Đường hầm QUIC Data Port %s đã được nối lại thành công!", gQuicDataPort)
	return nil
}

func TriggerNetworkRoaming() {
	alog("📡 [ANDROID-ROAMING] Hệ điều hành báo thay đổi mạng! Yêu cầu dò lại luồng QUIC...")
	mu.Lock()
	defer mu.Unlock()

	if session != nil { (*session).CloseWithError(0, ""); session = nil }
	if sessionUdp != nil { sessionUdp.Close(); sessionUdp = nil }

	go func() {
		mu.Lock()
		defer mu.Unlock()
		ReconnectSilently()
	}()
}

func getOutboundIP(targetIP string) string {
	if targetIP == "" || targetIP == "NONE" { return "NONE" }
	conn, err := net.Dial("udp", net.JoinHostPort(targetIP, "5555"))
	if err != nil { return "NONE" }
	defer conn.Close()
	localAddr := conn.LocalAddr().(*net.UDPAddr)
	return localAddr.IP.String()
}

func StartHeartbeat(activeIp string, authPort string) {
	mu.Lock(); if isHeartbeatRunning { mu.Unlock(); return }; isHeartbeatRunning = true; mu.Unlock()

	tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, KeepAlivePeriod: 10 * time.Second}

	var pingConn *quic.Conn
	ticker := time.NewTicker(20 * time.Second)
	defer func() { ticker.Stop(); mu.Lock(); isHeartbeatRunning = false; mu.Unlock() }()

	alog("💓 Kích hoạt luồng Heartbeat (Keep-Alive) cho Android. Chu kỳ: 20s")

	for range ticker.C {
		mu.Lock(); running := isHeartbeatRunning; mu.Unlock()
		if !running {
			if pingConn != nil { (*pingConn).CloseWithError(0, "Heartbeat Stopped") }
			break
		}
		if pingConn == nil || (*pingConn).Context().Err() != nil {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			pingConn, _, _ = dialQuic(ctx, "0.0.0.0", activeIp, authPort, tlsConf, config)
			cancel()
			if pingConn == nil { continue }
		}
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		stream, err := (*pingConn).OpenStreamSync(ctx)
		if err == nil {
			stream.Write([]byte("AUTH_REQ|PING"))
			stream.Close()
			io.ReadAll(stream)
		}
		cancel()
	}
}

func Logout() string {
	mu.Lock(); defer mu.Unlock()
	isHeartbeatRunning = false
	if session != nil { (*session).CloseWithError(0, "Client Logged Out"); session = nil }
	if sessionUdp != nil { sessionUdp.Close(); sessionUdp = nil }
	alog("👋 [LOGOUT] Đã đăng xuất! Hủy toàn bộ kết nối QUIC.")
	return "LOGOUT_SUCCESS"
}

func resolvePath(p string) string {
	if remoteRoot == "" { return p }
	cleanP := path.Clean("/" + p)
	if cleanP == "/" { return remoteRoot }
	if strings.HasPrefix(cleanP, remoteRoot) { return cleanP }
	return remoteRoot + cleanP
}

func pingUFW(localIp, targetIp, port string, testSize int) bool {
	laddr := &net.UDPAddr{IP: net.ParseIP(localIp), Port: 0}
	raddr, err := net.ResolveUDPAddr("udp", net.JoinHostPort(targetIp, port))
	if err != nil { return false }
	conn, err := net.ListenUDP("udp", laddr)
	if err != nil { return false }
	defer conn.Close()

	fakePayload := make([]byte, testSize)
	conn.SetDeadline(time.Now().Add(500 * time.Millisecond))
	_, err = conn.WriteToUDP(fakePayload, raddr)
	return err == nil
}

func ExecuteMTURadar(localIp, targetIp, testPort string) int {
	alog("==========================================================================")
	alog("📡 [MTU-RADAR] KÍCH HOẠT HỆ THỐNG TRINH SÁT MTU ĐỘNG (HOÀNG-HEURISTIC)")
	alog("==========================================================================")

	time.Sleep(500 * time.Millisecond)

	alog("📡 [MTU-RADAR][TRẦN-VẬT-LÝ] Đang phóng gói tin trinh sát kích cực đại: 1500 bytes...")
	if pingUFW(localIp, targetIp, testPort, 1500) {
		alog("🎉 [SUCCESS][MTU-RADAR] Tuyệt vời! Đường truyền thông suốt hoàn hảo mốc 1500 bytes!")
		return 1500
	}

	alog("⚠️ [WARNING][MTU-RADAR] Mốc 1500 bytes tịt ngòi! Kích hoạt chia đôi phân đoạn...")
	currentUpper := 1500
	currentLower := 1000
	lastSuccess := 1000

	for {
		if currentUpper-currentLower <= 1 {
			alog("🚨 [MTU-RADAR][SÀN-TỐI-THIỂU] Đã ép tới mốc %d nhưng vẫn không thông!", currentUpper)
			alog("📉 [RESULT-MTU] Ép cấu hình hạ tầng về mốc sàn an toàn: 1000 bytes.")
			return 1000
		}

		distance := (currentUpper - currentLower) / 2
		mid := currentLower + distance
		alog("📡 [MTU-RADAR][CHIA-ĐÔI] Khoảng cách: %d | Thử mốc trung vị: %d bytes...", distance*2, mid)

		if pingUFW(localIp, targetIp, testPort, mid) {
			alog("🎯 [SUCCESS][MTU-RADAR] Mốc trung vị %d bytes NGON LÀNH! Bắt đầu leo thang...", mid)
			lastSuccess = mid

			alog("📈 [MTU-RADAR][LEO-THANG-1] Kích nổ tiến trình quét hàng TRĂM (+100)...")
			for val := lastSuccess + 100; val < currentUpper; val += 100 {
				if pingUFW(localIp, targetIp, testPort, val) {
					alog("✅ [SUCCESS][LEO-THANG-1] Mốc %d bytes OK.", val)
					lastSuccess = val
				} else {
					alog("💥 [BURST][LEO-THANG-1] Mốc %d bytes BỊ CHẶN! Khóa trần mới = %d.", val, val)
					currentUpper = val
					break
				}
			}

			alog("📈 [MTU-RADAR][LEO-THANG-2] Kích nổ tiến trình quét hàng CHỤC (+10)...")
			for val := lastSuccess + 10; val < currentUpper; val += 10 {
				if pingUFW(localIp, targetIp, testPort, val) {
					alog("✅ [SUCCESS][LEO-THANG-2] Mốc %d bytes OK.", val)
					lastSuccess = val
				} else {
					alog("💥 [BURST][LEO-THANG-2] Mốc %d bytes BỊ CHẶN! Khóa trần mới = %d.", val, val)
					currentUpper = val
					break
				}
			}

			alog("📈 [MTU-RADAR][LEO-THANG-3] Kích nổ tiến trình quét hàng ĐƠN VỊ (+1)...")
			for val := lastSuccess + 1; val < currentUpper; val++ {
				if pingUFW(localIp, targetIp, testPort, val) {
					alog("✅ [SUCCESS][LEO-THANG-3] Mốc %d bytes OK.", val)
					lastSuccess = val
				} else {
					alog("💥 [BURST][LEO-THANG-3] Mốc %d bytes SẬP BẪY PHÂN MẢNH!")
					break
				}
			}
			break
		} else {
			alog("💥 [BURST][CHIA-ĐÔI] Gói %d bytes bị rớt. Co cụm trần về %d.", mid, mid)
			currentUpper = mid
		}
	}

	alog("==========================================================================")
	alog("🏆 [CHẾ ĐỘ TÍCH ỨNG] ĐÃ TÌM RA ĐỈNH MTU TỐI ƯU: [%d bytes]", lastSuccess)
	alog("==========================================================================")
	return lastSuccess
}

type dumbPacketConn struct{ c *net.UDPConn }
func (d *dumbPacketConn) ReadFrom(p []byte) (int, net.Addr, error) { return d.c.ReadFrom(p) }
func (d *dumbPacketConn) WriteTo(p []byte, addr net.Addr) (int, error) { return d.c.WriteTo(p, addr) }
func (d *dumbPacketConn) Close() error                       { return d.c.Close() }
func (d *dumbPacketConn) LocalAddr() net.Addr                { return d.c.LocalAddr() }
func (d *dumbPacketConn) SetDeadline(t time.Time) error      { return d.c.SetDeadline(t) }
func (d *dumbPacketConn) SetReadDeadline(t time.Time) error  { return d.c.SetReadDeadline(t) }
func (d *dumbPacketConn) SetWriteDeadline(t time.Time) error { return d.c.SetWriteDeadline(t) }

func dialQuic(ctx context.Context, localIp, targetIp, port string, tlsConf *tls.Config, config *quic.Config) (*quic.Conn, *net.UDPConn, error) {
	laddr := &net.UDPAddr{IP: net.ParseIP(localIp), Port: 0}
	raddr, err := net.ResolveUDPAddr("udp", net.JoinHostPort(targetIp, port))
	if err != nil { return nil, nil, err }
	udpConn, err := net.ListenUDP("udp", laddr)
	if err != nil { return nil, nil, err }
	wrappedConn := &dumbPacketConn{c: udpConn}
	conn, err := quic.Dial(ctx, wrappedConn, raddr, tlsConf, config)
	if err != nil { udpConn.Close(); return nil, nil, err }
	return conn, udpConn, nil
}

func InitializeQUIC(targetLanIp, targetTsIp, user, pass, hwid, authPort, masterKey, sniDomain string) string {
	alog("🚀 KHỞI ĐỘNG CHIẾN DỊCH VƯỢT TƯỜNG LỬA (PORT KNOCKING)")

	activeIp := targetTsIp
	connTCP, err := net.DialTimeout("tcp", net.JoinHostPort(targetLanIp, "22"), 1*time.Second)
	if err == nil { connTCP.Close(); activeIp = targetLanIp }

	gTlsConf = &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}, ServerName: sniDomain}
	gConfig = &quic.Config{MaxIdleTimeout: 120 * time.Second, HandshakeIdleTimeout: 10 * time.Second, KeepAlivePeriod: 10 * time.Second}

	myLanIp := getOutboundIP(targetLanIp)
	myTsIp := getOutboundIP(targetTsIp)

	nonceBytes := make([]byte, 8); rand.Read(nonceBytes)
	gAuthCmd = fmt.Sprintf("AUTH_REQ|USER:%s|PASS:%s|LAN:%s|TS:%s|HWID:%s|NONCE:%x", user, pass, myLanIp, myTsIp, hwid, nonceBytes)

	localIp := myTsIp
	if activeIp == targetLanIp { localIp = myLanIp }

	gActiveIp = activeIp
	gAuthPort = authPort

	authConn, authUdp, err := dialQuic(context.Background(), localIp, activeIp, authPort, gTlsConf, gConfig)
	if err != nil { return "ERROR|Knocking Failed" }

	authStream, err := (*authConn).OpenStreamSync(context.Background())
	if err != nil { (*authConn).CloseWithError(0, ""); authUdp.Close(); return "ERROR|Knocking Stream Failed" }
	authStream.Write([]byte(gAuthCmd)); authStream.Close()
	res, _ := io.ReadAll(authStream)
	(*authConn).CloseWithError(0, ""); authUdp.Close()

	resStr := string(res)
	if !strings.HasPrefix(resStr, "AUTH_SUCCESS") { return "ERROR|Auth Rejected" }

	parts := strings.Split(resStr, "|")
	var kcpDataPort string
	if len(parts) >= 4 {
		remoteRoot = parts[1]; gQuicDataPort = parts[2]; kcpDataPort = parts[3]
	} else { return "ERROR|Invalid Server Protocol Data" }

	errQuic := ReconnectSilently()
	if errQuic != nil {
		return "ERROR|Quic Data Stream Failed: " + errQuic.Error()
	}

	mtu := ExecuteMTURadar(localIp, activeIp, kcpDataPort)
	go StartHeartbeat(activeIp, authPort)

	if len(parts) >= 10 {
		return fmt.Sprintf("SUCCESS|%d|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s",
			mtu, activeIp, gQuicDataPort, kcpDataPort, remoteRoot,
			parts[4], parts[5], parts[6], parts[7], parts[8], parts[9],
		)
	}
	return fmt.Sprintf("SUCCESS|%d|%s|%s|%s|%s", mtu, activeIp, gQuicDataPort, kcpDataPort, remoteRoot)
}

func readQuicResponse(stream io.Reader) ([]byte, error) {
	res, err := io.ReadAll(stream); if err != nil { return nil, err }; return res, nil
}

func sendRawQuic(opcode byte, path string, offset uint64, reqLen uint32, data []byte) ([]byte, error) {
	mu.Lock(); sess := session; mu.Unlock()
	if sess == nil || (*sess).Context().Err() != nil {
		alog("❌ [QUIC-SEND] Session rỗng hoặc đã sụp! Cố gắng tự động Reconnect...")
		err := ReconnectSilently()
		if err != nil { return nil, fmt.Errorf("reconnect failed: %v", err) }
		mu.Lock(); sess = session; mu.Unlock()
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	alog("📤 [QUIC-SEND] Xin cấp Stream gửi Opcode 0x%02X, Path: %s", opcode, path)
	stream, err := (*sess).OpenStreamSync(ctx)
	if err != nil {
		alog("❌ [QUIC-SEND] Lỗi mở Stream: %v", err)
		return nil, err
	}

	buf := new(bytes.Buffer)
	reqId := atomic.AddUint64(&globalReqId, 1)

	binary.Write(buf, binary.LittleEndian, uint32(0x5A484941))
	binary.Write(buf, binary.LittleEndian, opcode)
	binary.Write(buf, binary.LittleEndian, reqId)
	binary.Write(buf, binary.LittleEndian, offset)
	dataLen := uint32(len(data)); if opcode == 0x03 { dataLen = reqLen }
	binary.Write(buf, binary.LittleEndian, dataLen)
	binary.Write(buf, binary.LittleEndian, uint16(len(path)))
	buf.WriteString(path)
	if data != nil { buf.Write(data) }

	_, err = stream.Write(buf.Bytes())
	if err != nil {
		alog("❌ [QUIC-SEND] Lỗi ghi dữ liệu vào Stream: %v", err)
		return nil, err
	}
	stream.Close()

	alog("⏳ [QUIC-WAIT] Đã đẩy %d bytes (ReqID: %d). Chờ Server phản hồi...", buf.Len(), reqId)

	res, readErr := readQuicResponse(stream)
	if readErr != nil {
		alog("❌ [QUIC-READ] Lỗi cúp cầu dao khi đang đọc: %v", readErr)
		return nil, readErr
	}

	alog("📥 [QUIC-READ] Nhận thành công %d bytes từ Server (ReqID: %d).", len(res), reqId)

	if len(res) < 27 {
		alog("❌ [QUIC-PARSE] Gói tin nát (Nhỏ hơn 27 bytes Header)!")
		return nil, fmt.Errorf("server error")
	}

	if res[4] == 0xFF {
		alog("❌ [QUIC-PARSE] Server trả mã 0xFF (File khóa hoặc lỗi IO)!")
		return nil, fmt.Errorf("server vfs io error")
	}

	return res[27:], nil
}

func formatErrorJson(err error) string { return fmt.Sprintf(`{"error": "%s"}`, err.Error()) }

//export StartQuicDataTunnel
func StartQuicDataTunnel() bool {
	return session != nil
}

func VfsStat(p string) string {
	alog("🔎 [QUIC-STAT] Đang dò thông tin: %s", p)
	res, err := sendRawQuic(0x01, resolvePath(p), 0, 0, nil)
	if err != nil {
		alog("❌ [QUIC-STAT] Bị lỗi: %v", err)
		return formatErrorJson(err)
	}
	if len(res) < 37 {
		alog("❌ [QUIC-STAT] Lỗi: Cục Payload quá ngắn (%d bytes)", len(res))
		return formatErrorJson(fmt.Errorf("payload too short"))
	}
	size := binary.LittleEndian.Uint64(res[0:8])
	isDir := res[8] == 1
	mtime := binary.LittleEndian.Uint64(res[9:17]) * 1000

	name := p; if idx := strings.LastIndex(p, "/"); idx >= 0 { name = p[idx+1:] }
	if name == "" { name = "/" }

	jsonStr := fmt.Sprintf(`{"name":"%s", "size":%d, "is_dir":%t, "last_modified":%d}`, name, size, isDir, mtime)
	alog("✅ [QUIC-STAT] Trả về: %s", jsonStr)
	return jsonStr
}

func VfsList(p string) string {
	alog("📂 [QUIC-LIST] Đang quét thư mục: %s", p)
	res, err := sendRawQuic(0x02, resolvePath(p), 0, 0, nil)
	if err != nil {
		alog("❌ [QUIC-LIST] Lỗi gửi lệnh: %v", err)
		return "[]"
	}

	var arr []string
	vSlash := p; if vSlash == "" { vSlash = "/" }
	if !strings.HasSuffix(vSlash, "/") { vSlash += "/" }

	offset := 0
	totalLen := len(res)

	for offset+15 <= totalLen {
		nameLen := int(binary.LittleEndian.Uint16(res[offset : offset+2]))
		isDirVal := res[offset+2]
		size := binary.LittleEndian.Uint64(res[offset+3 : offset+11])

		offset += 15

		if offset+nameLen > totalLen {
			alog("❌ [QUIC-LIST] Lỗi: Dữ liệu tên file bị xén mất!")
			break
		}

		name := string(res[offset : offset+nameLen])
		offset += nameLen

		isDirStr := "false"; if isDirVal == 1 { isDirStr = "true" }
		fileName := strings.ReplaceAll(name, "\"", "\\\"")

		arr = append(arr, fmt.Sprintf(`{"name":"%s","is_dir":%s,"size":%d,"path":"%s%s"}`, fileName, isDirStr, size, vSlash, fileName))
	}

	jsonArray := "[" + strings.Join(arr, ",") + "]"
	alog("✅ [QUIC-LIST] Quét xong! Trả về %d mục.", len(arr))
	return jsonArray
}

func VfsMkdir(p string) bool                               { _, err := sendRawQuic(0x05, resolvePath(p), 0, 0, nil); return err == nil }
func VfsDelete(p string) bool                              { _, err := sendRawQuic(0x06, resolvePath(p), 0, 0, nil); return err == nil }
func VfsCreate(p string) bool                              { _, err := sendRawQuic(0x04, resolvePath(p), 0, 0, []byte{}); return err == nil }
func VfsWrite(p string, offset int64, data []byte) bool    { _, err := sendRawQuic(0x04, resolvePath(p), uint64(offset), 0, data); return err == nil }
func VfsRename(oldP, newP string) bool                     { _, err := sendRawQuic(0x07, resolvePath(oldP), 0, 0, []byte(resolvePath(newP))); return err == nil }
func VfsDownload(remoteP string, localP string) bool {
	realPath := resolvePath(remoteP)
	statRes, err := sendRawQuic(0x01, realPath, 0, 0, nil)
	if err != nil || len(statRes) < 17 { return false }
	size := binary.LittleEndian.Uint64(statRes[0:8])
	f, err := os.Create(localP)
	if err != nil { return false }
	defer f.Close()

	var offset uint64 = 0
	chunk := uint32(256 * 1024)
	for offset < size {
		reqLen := chunk; if size-offset < uint64(chunk) { reqLen = uint32(size - offset) }
		data, err := sendRawQuic(0x03, realPath, offset, reqLen, nil)
		if err != nil { return false }
		f.Write(data)
		offset += uint64(len(data))
	}
	return true
}