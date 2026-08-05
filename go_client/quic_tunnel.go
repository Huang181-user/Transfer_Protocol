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

type QuicTunnel struct {
	AssignedPath   string
	DynamicKcpPort int // 🔥 Biến chứa Port KCP động Server cấp
	KcpNoDelay     int
	KcpInterval    int
	KcpResend      int
	KcpNc          int
	KcpSndWnd      int
	KcpRcvWnd      int
	Session        *quic.Conn
	activeIp       string
	authPort       string
	dataPort       string
	authCmd        string
	mountKcpDrive  string
	mountQuicDrive string
	mu             sync.Mutex
	limiter        chan struct{}
}

func GetTimestamp() string { return time.Now().Format("2006-01-02 15:04:05.000") }

func NewQuicTunnel(ip, authPort, dataPort, authCmd, kcpDrive, quicDrive string) *QuicTunnel {
	return &QuicTunnel{
		activeIp:       ip,
		authPort:       authPort,
		dataPort:       dataPort,
		authCmd:        authCmd,
		mountKcpDrive:  kcpDrive,
		mountQuicDrive: quicDrive,
		limiter:        make(chan struct{}, 5000),
	}
}

func DiscoverBestRoute(cfg *ClientConfig) string {
	pingTest := func(ip string) bool {
		if ip == "" || ip == "NONE" {
			return false
		}
		authPort := cfg.AuthPort
		if authPort == "" {
			authPort = "5555"
		}
		tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
		ctx, cancel := context.WithTimeout(context.Background(), 2000*time.Millisecond)
		defer cancel()
		conn, err := quic.DialAddr(ctx, net.JoinHostPort(ip, authPort), tlsConf, &quic.Config{HandshakeIdleTimeout: 2000 * time.Millisecond})
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

	log.Printf("[%s] [QUIC-TUNNEL] Initiating background QUIC connection (Target: %s, AuthPort: %s, DataPort: %s)...", GetTimestamp(), t.activeIp, t.authPort, t.dataPort)
	tlsConf := &tls.Config{InsecureSkipVerify: true, NextProtos: []string{"zhiauth-raw-quic"}}
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, HandshakeIdleTimeout: 30 * time.Second, KeepAlivePeriod: 10 * time.Second, MaxIncomingStreams: 2000}

	authConn, err := quic.DialAddr(context.Background(), net.JoinHostPort(t.activeIp, t.authPort), tlsConf, config)
	if err != nil {
		log.Printf("[%s] [QUIC-TUNNEL-ERROR] Failed to connect to authentication port %s: %v", GetTimestamp(), t.authPort, err)
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
		return fmt.Errorf("re-auth failure: %s", resStr)
	}

	// 🔥 BÓC TÁCH CHUỖI ĐỘNG: AUTH_SUCCESS|sharedPath|quicPort|kcpPort|nodelay|interval|resend|nc|snd_wnd|rcv_wnd
	parts := strings.Split(resStr, "|")
	if len(parts) >= 2 && parts[1] != "" {
		t.AssignedPath = parts[1]
	}
	if len(parts) >= 4 {
		t.dataPort = parts[2] // Đổi luôn Port QUIC hiện tại
		kcpPort, _ := strconv.Atoi(parts[3])
		t.DynamicKcpPort = kcpPort // Ghi nhớ KCP Port để main.go nổ máy
	}

	// Giá trị mặc định an toàn (fallback)
	t.KcpNoDelay = 1; t.KcpInterval = 10; t.KcpResend = 2; t.KcpNc = 0; t.KcpSndWnd = 512; t.KcpRcvWnd = 512

	// Nắn bóc các tham số KCP Tuning nếu Server trả về đủ 10 phần
	if len(parts) >= 10 {
		if nd, err := strconv.Atoi(parts[4]); err == nil { t.KcpNoDelay = nd }
		if it, err := strconv.Atoi(parts[5]); err == nil { t.KcpInterval = it }
		if rs, err := strconv.Atoi(parts[6]); err == nil { t.KcpResend = rs }
		if nc, err := strconv.Atoi(parts[7]); err == nil { t.KcpNc = nc }
		if sw, err := strconv.Atoi(parts[8]); err == nil { t.KcpSndWnd = sw }
		if rw, err := strconv.Atoi(parts[9]); err == nil { t.KcpRcvWnd = rw }
	}

	time.Sleep(2 * time.Millisecond)

	dataConn, err := quic.DialAddr(context.Background(), net.JoinHostPort(t.activeIp, t.dataPort), tlsConf, config)
	if err != nil {
		log.Printf("[%s] [QUIC-TUNNEL-ERROR] Failed to connect to data port %s: %v", GetTimestamp(), t.dataPort, err)
		return err
	}

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

		go func(s *quic.Stream) {
			defer s.Close()
			data, _ := io.ReadAll(s)
			msg := string(data)
			if strings.Contains(msg, "timed out") {
				if t.Session != nil {
					t.Session.CloseWithError(0, "timeout")
				}
			} else if strings.Contains(msg, "signed out") {
				log.Printf("\n[%s] [REMOTE-SIG] 💣 Nhận lệnh tử thần từ Server! Xóa sổ ổ đĩa %s: và %s:!", GetTimestamp(), t.mountKcpDrive, t.mountQuicDrive)
				exec.Command("net", "use", t.mountKcpDrive+":", "/delete", "/y").Run()
				exec.Command("net", "use", t.mountQuicDrive+":", "/delete", "/y").Run()
				os.Exit(0)
			}
		}(stream)
	}
}

func (t *QuicTunnel) SendFsCommandRaw(payload []byte) ([]byte, error) {
	t.limiter <- struct{}{}
	defer func() { <-t.limiter }()

	if t.Session == nil || t.Session.Context().Err() != nil {
		if err := t.ReconnectSilently(); err != nil {
			return nil, err
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	stream, err := t.Session.OpenStreamSync(ctx)
	if err != nil {
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
		return nil, err
	}

	if len(data) < 27 {
		return nil, fmt.Errorf("QUIC packet broken")
	}
	if data[4] == OP_ERROR {
		return nil, fmt.Errorf("Server VFS Error")
	}

	return data[27:], nil
}