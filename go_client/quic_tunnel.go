package main

import (
	"context"
	"crypto/tls"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/quic-go/quic-go"
)

type KcpTuningParams struct {
	NoDelay   int
	Interval  int
	Resend    int
	Nc        int
	SndWnd    int
	RcvWnd    int
	IsDynamic bool // Đánh dấu xem có phải do Server cấp động hay không
}

type QuicTunnel struct {
	AssignedPath     string
	AssignedQuicPort string
	AssignedKcpPort  string
	Tuning           KcpTuningParams // 🔥 LƯU THAM SỐ DYNAMIC KCP
	Session          *quic.Conn
	activeIp         string
	authPort         string
	authCmd          string
	mu               sync.Mutex
	limiter          chan struct{}
}

func GetTimestamp() string { return time.Now().Format("2006-01-02 15:04:05.000") }

func NewQuicTunnel(ip, authPort, authCmd string) *QuicTunnel {
	return &QuicTunnel{activeIp: ip, authPort: authPort, authCmd: authCmd, limiter: make(chan struct{}, 5000)}
}

func DiscoverBestRoute(cfg *ClientConfig) string {
	pingTest := func(ip string) bool {
		if ip == "" || ip == "NONE" {
			return false
		}
		tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
		ctx, cancel := context.WithTimeout(context.Background(), 2000*time.Millisecond)
		defer cancel()
		conn, err := quic.DialAddr(ctx, net.JoinHostPort(ip, cfg.AuthPort), tlsConf, &quic.Config{HandshakeIdleTimeout: 2000 * time.Millisecond})
		if err != nil {
			return false
		}
		stream, err := conn.OpenStreamSync(ctx)
		if err == nil {
			stream.Write([]byte("AUTH_REQ|PING"))
			stream.Close()
			res := make([]byte, 4)
			stream.Read(res)
		}
		conn.CloseWithError(0, "")
		return true
	}
	if pingTest(cfg.ServerLanIp) {
		return cfg.ServerLanIp
	}
	if pingTest(cfg.ServerTsIp) {
		return cfg.ServerTsIp
	}
	return ""
}

func (t *QuicTunnel) ReconnectSilently() error {
	t.mu.Lock()
	defer t.mu.Unlock()

	if t.Session != nil && t.Session.Context().Err() == nil {
		return nil
	}

	log.Printf("[%s] [QUIC-TUNNEL] 🚨 Kích hoạt luồng kết nối QUIC ngầm...", GetTimestamp())
	tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, HandshakeIdleTimeout: 30 * time.Second, KeepAlivePeriod: 10 * time.Second, MaxIncomingStreams: 2000}

	authConn, err := quic.DialAddr(context.Background(), net.JoinHostPort(t.activeIp, t.authPort), tlsConf, config)
	if err != nil {
		return err
	}

	authStream, err := authConn.OpenStreamSync(context.Background())
	if err != nil {
		authConn.CloseWithError(0, "")
		return err
	}

	authStream.Write([]byte(t.authCmd))
	authStream.Close()

	res, _ := io.ReadAll(authStream)
	authConn.CloseWithError(0, "")

	resStr := string(res)
	if !strings.HasPrefix(resStr, "AUTH_SUCCESS") {
		return fmt.Errorf("re-auth failure")
	}

	parts := strings.Split(resStr, "|")
	if len(parts) >= 4 {
		t.AssignedPath = parts[1]
		t.AssignedQuicPort = parts[2]
		t.AssignedKcpPort = parts[3]

		// 🔥 KIỂM TRA XEM SERVER CÓ GỬI KCP TUNING DYNAMIC KHÔNG (Phiên bản >= 10 parts)
		if len(parts) >= 10 {
			t.Tuning.NoDelay, _ = strconv.Atoi(parts[4])
			t.Tuning.Interval, _ = strconv.Atoi(parts[5])
			t.Tuning.Resend, _ = strconv.Atoi(parts[6])
			t.Tuning.Nc, _ = strconv.Atoi(parts[7])
			t.Tuning.SndWnd, _ = strconv.Atoi(parts[8])
			t.Tuning.RcvWnd, _ = strconv.Atoi(parts[9])
			t.Tuning.IsDynamic = true

			log.Printf("[%s] [DYNAMIC-KCP] 🎯 Đã tiếp nhận KCP Tuning từ Server: NoDelay=%d, Interval=%dms, Resend=%d, NC=%d, SND_WND=%d, RCV_WND=%d",
				GetTimestamp(), t.Tuning.NoDelay, t.Tuning.Interval, t.Tuning.Resend, t.Tuning.Nc, t.Tuning.SndWnd, t.Tuning.RcvWnd)
		} else {
			log.Printf("[%s] [DYNAMIC-KCP] ⚠️ Server không gửi Tuning params. Client sẽ xài cấu hình mặc định/hardcode!", GetTimestamp())
			t.Tuning.IsDynamic = false
		}
	}

	time.Sleep(2 * time.Millisecond)

	dataConn, err := quic.DialAddr(context.Background(), net.JoinHostPort(t.activeIp, t.AssignedQuicPort), tlsConf, config)
	if err != nil {
		return err
	}

	// 🔥 Đã fix: Gán trực tiếp vì dataConn vốn là *quic.Conn rồi
	t.Session = dataConn
	go t.ListenForServerSignals()
	return nil
}

func (t *QuicTunnel) ListenForServerSignals() {
	for {
		if t.Session == nil {
			return
		}
		stream, err := t.Session.AcceptStream(context.Background())
		if err != nil {
			return
		}

		go func(s *quic.Stream) { // 🔥 Khôi phục lại con trỏ
			defer s.Close()
			data, _ := io.ReadAll(s)
			msg := string(data)
			if strings.Contains(msg, "timed out") {
				if t.Session != nil {
					t.Session.CloseWithError(0, "timeout")
				}
			} else if strings.Contains(msg, "signed out") {
				exec.Command("sudo", "umount", "-l", "/mnt/Cloud/QUIC_DRIVE").Run()
				exec.Command("sudo", "umount", "-l", "/mnt/Cloud/VFS_DRIVE").Run()
				os.Exit(0)
			}
		}(stream)
	}
}

func (t *QuicTunnel) SendFsCommandRaw(payload []byte) ([]byte, error) {
	t.limiter <- struct{}{}
	defer func() { <-t.limiter }()

	if t.Session == nil || t.Session.Context().Err() != nil {
		log.Printf("[%s] [QUIC-TUNNEL] [AUTO-RECONNECT] Mất kết nối QUIC. Đang khôi phục...", GetTimestamp())
		if err := t.ReconnectSilently(); err != nil {
			log.Printf("[%s] [QUIC-ERR] Không thể Reconnect: %v", GetTimestamp(), err)
			return nil, err
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	stream, err := t.Session.OpenStreamSync(ctx)
	if err != nil {
		log.Printf("[%s] [QUIC-ERR] OpenStreamSync lỗi: %v", GetTimestamp(), err)
		if err := t.ReconnectSilently(); err == nil {
			ctx2, cancel2 := context.WithTimeout(context.Background(), 30*time.Second)
			defer cancel2()
			stream, err = t.Session.OpenStreamSync(ctx2)
		}
		if err != nil {
			return nil, err
		}
	}

	stream.Write(payload)
	stream.Close()

	data, err := io.ReadAll(stream)
	if err != nil {
		log.Printf("[%s] [QUIC-ERR] Đọc luồng dữ liệu lỗi: %v", GetTimestamp(), err)
		return nil, err
	}

	if len(data) == 0 {
		return nil, fmt.Errorf("QUIC packet empty (Stream Closed by Server)")
	}
	if len(data) < 27 {
		return nil, fmt.Errorf("QUIC packet broken (Len: %d)", len(data))
	}
	if data[4] == OP_ERROR {
		return nil, fmt.Errorf("Server VFS Error")
	}

	return data[27:], nil
}

func (t *QuicTunnel) StartHeartbeat() {
	tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, KeepAlivePeriod: 10 * time.Second}

	var pingConn *quic.Conn // 🔥 Khôi phục lại con trỏ

	ticker := time.NewTicker(15 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		if pingConn == nil || pingConn.Context().Err() != nil {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			connObj, dialErr := quic.DialAddr(ctx, net.JoinHostPort(t.activeIp, t.authPort), tlsConf, config)
			cancel()
			if dialErr != nil {
				continue
			}
			pingConn = connObj // 🔥 Bỏ dấu & vì connObj đã là *quic.Conn
		}

		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		stream, err := pingConn.OpenStreamSync(ctx)
		if err == nil {
			stream.Write([]byte("AUTH_REQ|PING"))
			stream.Close()
			io.ReadAll(stream)
		} else {
			pingConn.CloseWithError(0, "")
		}
		cancel()
	}
}
