//go:build windows
package main

import (
	"log"
	"time"
)

type FileMeta struct { IsDir bool; Size uint64; Mode uint32 }

func StartDualFuseSubsystem(kcpDrive, quicDrive, remoteBase string, tunnel *QuicTunnel) {
	ts := time.Now().Format("2006-01-02 15:04:05.000")
	log.Printf("[%s] ==========================================================================", ts)
	log.Printf("[%s] 🎉 [BINGO] Hầm mạng đã thông suốt tới Server!", ts)
	log.Printf("[%s] 👉 Kích nổ hệ thống WinFSP Kernel Dual-Mount (KCP & QUIC) lên ổ đĩa %s: và %s:", ts, kcpDrive, quicDrive)
	log.Printf("[%s] ==========================================================================", ts)

	// Khởi động 2 ổ Kernel FUSE độc lập cực mạnh
	MountWinFspDrive(kcpDrive, remoteBase, tunnel, "KCP")
	time.Sleep(500 * time.Millisecond)
	MountWinFspDrive(quicDrive, remoteBase, tunnel, "QUIC")
}

func InjectBulkCache(parentPath string, bulkData string) {}
func CheckRAMCache(fullPath string) (FileMeta, bool) { return FileMeta{}, false }
func RemoveFromRAMCache(fullPath string) {}