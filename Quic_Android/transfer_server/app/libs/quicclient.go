package quicclient

import (
	"crypto/tls"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"

	"github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	_ "golang.org/x/mobile/bind"
)

// 🎯 Tách hàm tạo Transport riêng để chủ động đóng (Close) sau khi dùng xong
func createQuicTransport(mtu int, sni string) *http3.Transport {
	return &http3.Transport{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: true,
			ServerName:         sni,
		},
		QUICConfig: &quic.Config{
			InitialPacketSize:    uint16(mtu),
			HandshakeIdleTimeout: 5 * time.Second,
		},
	}
}

func ProbeMTU(url string, size int, sni string) bool {
	tr := createQuicTransport(size, sni)
	defer tr.Close() // 🔴 QUAN TRỌNG: Đóng ống QUIC để dọn dẹp Goroutine & Socket

	client := &http.Client{Transport: tr, Timeout: 5 * time.Second}
	resp, err := client.Head(url)
	if err != nil {
		return false
	}
	defer resp.Body.Close()
	return resp.StatusCode == http.StatusOK
}

func FetchJson(url string, mtu int, sni string) string {
	tr := createQuicTransport(mtu, sni)
	defer tr.Close() // 🔴 QUAN TRỌNG

	client := &http.Client{Transport: tr, Timeout: 30 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		return "ERROR: " + err.Error()
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	return string(body)
}

func DownloadFast(url string, savePath string, mtu int, sni string) string {
	tr := createQuicTransport(mtu, sni)
	defer tr.Close() // 🔴 QUAN TRỌNG

	// Không giới hạn Timeout tổng cho việc tải file lớn, chỉ giới hạn lúc bắt tay
	client := &http.Client{Transport: tr}
	resp, err := client.Get(url)
	if err != nil {
		return "ERROR_CONNECT: " + err.Error()
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return fmt.Sprintf("ERROR_HTTP: %d", resp.StatusCode)
	}

	out, err := os.Create(savePath)
	if err != nil {
		return "ERROR_FILE: " + err.Error()
	}
	defer out.Close()

	n, err := io.Copy(out, resp.Body)
	if err != nil {
		return "ERROR_COPY: " + err.Error()
	}
	return fmt.Sprintf("SUCCESS|%d", n)
}
