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
	"crypto/rand"
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

	"github.com/quic-go/quic-go"
	"golang.org/x/crypto/chacha20poly1305"
)

var (
	session            *quic.Conn
	sessionUdp         *net.UDPConn
	mu                 sync.Mutex
	globalReqId        uint64
	remoteRoot         string
	isHeartbeatRunning bool
)

func alog(format string, args ...interface{}) {
	now := time.Now().Format("15:04:05.000")
	msg := fmt.Sprintf(format, args...)
	cstr := C.CString(fmt.Sprintf("[HUANG_GO_CORE] [%s] %s", now, msg))
	C.nativeLog(cstr)
	C.free(unsafe.Pointer(cstr))
}

func TriggerNetworkRoaming() {
	alog("📡 [ANDROID-ROAMING] Hệ điều hành báo thay đổi mạng! Yêu cầu dò lại luồng...")
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

// Bắn UDP Radar độc lập bằng Go (đỡ phải code bên C++)
func pingKCP(localIp, targetIp, port string, mtu int, masterKey string) bool {
	paddingSize := mtu - 107
	if paddingSize < 0 { return false }
	plaintext := make([]byte, 27+paddingSize)
	binary.LittleEndian.PutUint32(plaintext[0:4], 0x5A484941)
	binary.LittleEndian.PutUint32(plaintext[21:25], uint32(paddingSize))
	
	keyBytes := []byte(masterKey)
	if len(keyBytes) > 32 { keyBytes = keyBytes[:32] }
	if len(keyBytes) < 32 {
		paddedKey := make([]byte, 32); copy(paddedKey, keyBytes); keyBytes = paddedKey
	}
	aead, err := chacha20poly1305.New(keyBytes)
	if err != nil { return false }

	nonce := make([]byte, chacha20poly1305.NonceSize); rand.Read(nonce)
	ciphertext := aead.Seal(nil, nonce, plaintext, nil)
	libsodiumPayload := append(nonce, ciphertext...)

	kcpPacket := make([]byte, 24+len(libsodiumPayload))
	binary.LittleEndian.PutUint32(kcpPacket[0:4], 0x99887766)
	kcpPacket[4] = 81
	binary.LittleEndian.PutUint16(kcpPacket[6:8], 65535)
	binary.LittleEndian.PutUint32(kcpPacket[8:12], uint32(time.Now().UnixMilli()))
	binary.LittleEndian.PutUint32(kcpPacket[20:24], uint32(len(libsodiumPayload)))
	copy(kcpPacket[24:], libsodiumPayload)

	laddr := &net.UDPAddr{IP: net.ParseIP(localIp), Port: 0}
	raddr, err := net.ResolveUDPAddr("udp", net.JoinHostPort(targetIp, port))
	if err != nil { return false }
	conn, err := net.ListenUDP("udp", laddr)
	if err != nil { return false }
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(500 * time.Millisecond))
	_, err = conn.WriteToUDP(kcpPacket, raddr)
	if err != nil { return false }
	recvBuf := make([]byte, mtu+100)
	_, _, err = conn.ReadFromUDP(recvBuf)
	return err == nil
}

func ExecuteMTURadar(localIp, targetIp, kcpPort, masterKey string) int {
	alog("📡 [KCP-RADAR] Kích hoạt Radar dò MTU xuyên UFW vào cổng %s", kcpPort)
	bestMTU := 1000; minMTU := 1000; maxMTU := 1500
	if pingKCP(localIp, targetIp, kcpPort, maxMTU, masterKey) { return maxMTU }
	for minMTU <= maxMTU {
		midMTU := minMTU + (maxMTU-minMTU)/2
		if pingKCP(localIp, targetIp, kcpPort, midMTU, masterKey) {
			bestMTU = midMTU; minMTU = midMTU + 1
		} else { maxMTU = midMTU - 1 }
	}
	return bestMTU
}

type dumbPacketConn struct { c *net.UDPConn }
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

	tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}, ServerName: sniDomain}
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, HandshakeIdleTimeout: 10 * time.Second, KeepAlivePeriod: 10 * time.Second}

	myLanIp := getOutboundIP(targetLanIp)
	myTsIp := getOutboundIP(targetTsIp)

	nonceBytes := make([]byte, 8); rand.Read(nonceBytes)
	authCmd := fmt.Sprintf("AUTH_REQ|USER:%s|PASS:%s|LAN:%s|TS:%s|HWID:%s|NONCE:%x", user, pass, myLanIp, myTsIp, hwid, nonceBytes)

	localIp := myTsIp
	if activeIp == targetLanIp { localIp = myLanIp }

	authConn, authUdp, err := dialQuic(context.Background(), localIp, activeIp, authPort, tlsConf, config)
	if err != nil { return "ERROR|Knocking Failed" }

	authStream, err := (*authConn).OpenStreamSync(context.Background())
	if err != nil { (*authConn).CloseWithError(0, ""); authUdp.Close(); return "ERROR|Knocking Stream Failed" }
	authStream.Write([]byte(authCmd)); authStream.Close()
	res, _ := io.ReadAll(authStream)
	(*authConn).CloseWithError(0, ""); authUdp.Close()

	resStr := string(res)
	if !strings.HasPrefix(resStr, "AUTH_SUCCESS") { return "ERROR|Auth Rejected" }

	parts := strings.Split(resStr, "|")
	var quicDataPort, kcpDataPort string
	if len(parts) >= 4 {
		remoteRoot = parts[1]; quicDataPort = parts[2]; kcpDataPort = parts[3]
	} else { return "ERROR|Invalid Server Protocol Data" }

	time.Sleep(1500 * time.Millisecond)

	var dataConn *quic.Conn
	var dataUdp *net.UDPConn
	for i := 1; i <= 3; i++ {
		ctxDial, cancelDial := context.WithTimeout(context.Background(), 5*time.Second)
		dataConn, dataUdp, err = dialQuic(ctxDial, localIp, activeIp, quicDataPort, tlsConf, config)
		cancelDial()
		if err == nil { break }
		time.Sleep(1 * time.Second)
	}

	mtu := ExecuteMTURadar(localIp, activeIp, kcpDataPort, masterKey)

	mu.Lock()
	if session != nil { (*session).CloseWithError(0, "") }
	if sessionUdp != nil { sessionUdp.Close() }
	session = dataConn; sessionUdp = dataUdp
	mu.Unlock()

	go StartHeartbeat(activeIp, authPort)
	return fmt.Sprintf("SUCCESS|%d|%s|%s|%s|%s", mtu, activeIp, quicDataPort, kcpDataPort, remoteRoot)
}

func readQuicResponse(stream io.Reader) ([]byte, error) {
	res, err := io.ReadAll(stream); if err != nil { return nil, err }; return res, nil
}

func sendRawQuic(opcode byte, path string, offset uint64, reqLen uint32, data []byte) ([]byte, error) {
	mu.Lock(); sess := session; mu.Unlock()
	if sess == nil { return nil, fmt.Errorf("no quic session") }

	start := time.Now()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second); defer cancel()

	stream, err := (*sess).OpenStreamSync(ctx)
	if err != nil { return nil, err }
	defer stream.Close()

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

	stream.Write(buf.Bytes()); stream.Close()
	res, readErr := readQuicResponse(stream)
	if readErr != nil { return nil, readErr }
	if len(res) < 27 { return nil, fmt.Errorf("server error") }
	_ = start // Tắt tiếng báo lỗi của compiler
	return res[27:], nil
}

func formatErrorJson(err error) string { return fmt.Sprintf(`{"error": "%s"}`, err.Error()) }

func VfsStat(p string) string {
	res, err := sendRawQuic(0x01, resolvePath(p), 0, 0, nil)
	if err != nil { return formatErrorJson(err) }
	size := binary.LittleEndian.Uint64(res[0:8])
	isDir := res[8] == 1
	name := p; if idx := strings.LastIndex(p, "/"); idx >= 0 { name = p[idx+1:] }
	if name == "" { name = "/" }
	return fmt.Sprintf(`{"name":"%s", "size":%d, "is_dir":%t}`, name, size, isDir)
}

func VfsList(p string) string {
	res, err := sendRawQuic(0x02, resolvePath(p), 0, 0, nil)
	if err != nil { return "[]" }
	cleanStr := strings.TrimSpace(strings.TrimRight(string(res), "\x00"))
	var arr []string
	vSlash := p; if vSlash == "" { vSlash = "/" }
	if !strings.HasSuffix(vSlash, "/") { vSlash += "/" }
	for _, t := range strings.Split(cleanStr, "|") {
		t = strings.TrimSpace(t); if len(t) == 0 { continue }
		parts := strings.Split(t, ",")
		if len(parts) >= 2 {
			isDir := "false"; if strings.TrimSpace(parts[1]) == "DIR" { isDir = "true" }
			fileName := strings.ReplaceAll(strings.TrimSpace(parts[0]), "\"", "")
			fileSize := "0"; if len(parts) >= 3 { fileSize = strings.ReplaceAll(strings.TrimSpace(parts[2]), "\"", "") }
			arr = append(arr, fmt.Sprintf(`{"name":"%s","is_dir":%s,"size":%s,"path":"%s%s"}`, fileName, isDir, fileSize, vSlash, fileName))
		}
	}
	return "[" + strings.Join(arr, ",") + "]"
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
