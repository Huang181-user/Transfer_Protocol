package main

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/tls"
	"crypto/x509"
	"log"
	"math/big"
	"net"
	"strconv"
	"time"

	"github.com/quic-go/quic-go"
)

func StartRawQuicListener() {
	log.Println("[SERVER-QUIC] Deploying DUAL raw UDP socket bindings (Port Knocking Mode)...")
	authPortStr := strconv.Itoa(globalConfig.Network.AuthPort)
	quicDataPortStr := strconv.Itoa(globalConfig.Network.QuicDataPort)

	go spawnListener(authPortStr, "AUTH_PORT")
	go spawnListener(quicDataPortStr, "DATA_PORT")
	select {}
}

func spawnListener(port string, role string) {
	config := &quic.Config{MaxIdleTimeout: 120 * time.Second, KeepAlivePeriod: 15 * time.Second, MaxIncomingStreams: 10000}
	listener, err := quic.ListenAddr(":"+port, generateTLSConfig(), config)
	if err != nil {
		log.Fatalf("[SERVER-FATAL] Failed to lock down port %s (%s): %v", port, role, err)
	}
	defer listener.Close()
	log.Printf("[SERVER-SUCCESS] %s LISTENER OPERATIONAL ON PORT %s!", role, port)

	for {
		conn, err := listener.Accept(context.Background())
		if err != nil {
			continue
		}
		go handleClientSession(conn, port)
	}
}

func handleClientSession(conn *quic.Conn, port string) {
	remoteIP := (*conn).RemoteAddr().(*net.UDPAddr).IP.String()

	defer func() {
		(*conn).CloseWithError(0, "session terminated cleanly")
		quicDataPortStr := strconv.Itoa(globalConfig.Network.QuicDataPort)
		if port == quicDataPortStr {
			log.Printf("[QUIC-DISCONNECT] 🚨 Phát hiện hầm DATA cổng %s bị sập từ IP: %s. Kích hoạt xả trạm an toàn...", port, remoteIP)
			TriggerServerSessionCleanup(remoteIP)
		}
	}()

	for {
		stream, err := (*conn).AcceptStream(context.Background())
		if err != nil {
			return
		}
		go HandleIncomingStream(stream, port, conn)
	}
}

func generateTLSConfig() *tls.Config {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		panic(err)
	}
	template := x509.Certificate{SerialNumber: big.NewInt(1)}
	certDER, err := x509.CreateCertificate(rand.Reader, &template, &template, &key.PublicKey, key)
	if err != nil {
		panic(err)
	}
	tlsCert := tls.Certificate{Certificate: [][]byte{certDER}, PrivateKey: key}
	return &tls.Config{Certificates: []tls.Certificate{tlsCert}, NextProtos: []string{"zhiauth-raw-quic"}}
}
