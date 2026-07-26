package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#cgo LDFLAGS: -L../build -lzhiauth_core -lstdc++ -lsqlite3
#include "auth_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"
	"unsafe"
)

func main() {
	// 1. Tải cấu hình trước tiên
	LoadConfig("../config/config.json")

	// 2. Khởi tạo Logger dựa trên cấu hình động
	logPath := globalConfig.Paths.LogPath
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0666)
	if err == nil {
		os.Chmod(logPath, 0666) 
		multiWriter := io.MultiWriter(os.Stdout, logFile)
		log.SetOutput(multiWriter)
	}

	log.Println("==========================================================================")
	log.Println("🚀 ZHIAUTH HYBRID DAEMON - DYNAMIC CONFIG & NO HARDCODE v6.0")
	log.Println("==========================================================================")

	// 3. Đẩy tham số bảo mật và network xuống lõi C++ qua CGO
	cDbPath := C.CString("../" + globalConfig.Paths.Database)
	cMasterKey := C.CString(globalConfig.Security.MasterSymKey)
	defer C.free(unsafe.Pointer(cDbPath))
	defer C.free(unsafe.Pointer(cMasterKey))

	C.zhiauth_core_init(cDbPath, cMasterKey, C.int(globalConfig.Network.KcpDataPort), C.int(globalConfig.Network.QuicDataPort))

	go StartGlobalWatchdog()
	go StartRawQuicListener()

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, os.Interrupt, syscall.SIGTERM)
	<-sigChan
	
	log.Println("\n[SYSTEM-HALT] 💀 Đang rút ống thở và dọn dẹp hệ thống...")
	os.Exit(0)
}
