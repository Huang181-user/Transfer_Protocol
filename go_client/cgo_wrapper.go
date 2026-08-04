package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#cgo LDFLAGS: -L../build -lzhiauth_client_core -lstdc++ -lsodium -lpthread -lws2_32 -lcredui -liphlpapi
#include "client_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"bytes"
	"encoding/binary"
	"fmt"
	"hash/crc32"
	"log"
	"sync/atomic"
	"time"
	"unsafe"
)

const (
	OP_PING     = 0x00
	OP_STAT     = 0x01
	OP_LIST     = 0x02
	OP_READ     = 0x03
	OP_WRITE    = 0x04
	OP_MKDIR    = 0x05
	OP_DELETE   = 0x06
	OP_RENAME   = 0x07
	OP_TRUNCATE = 0x08
	OP_ERROR    = 0xFF
)

var globalReqId uint64 = 0
var globalClientId uint32 = 0

// 🔥 TẠO MỚI: Khởi tạo Client ID độc nhất dựa trên HWID
func InitClientID(hwid string) {
	globalClientId = crc32.ChecksumIEEE([]byte(hwid))
}

// Truyền mượt mà Conv xuống Backend C++
func InitCppSDK(ip string, port int, symKey string, mtu int, conv uint32) bool {
	ts := time.Now().Format("2006-01-02 15:04:05.000")
	log.Printf("[%s] [CGO-WRAPPER] Đang đánh thức quái vật KCP C++ (Conv: %d)...", ts, conv)

	cIP, cKey := C.CString(ip), C.CString(symKey)
	defer C.free(unsafe.Pointer(cIP))
	defer C.free(unsafe.Pointer(cKey))
	
	return C.zhiauth_client_init(cIP, C.int(port), cKey, C.int(mtu), C.uint32_t(conv)) == 0
}

func ShutdownCppSDK() { 
	C.zhiauth_client_shutdown() 
}

func BuildVfsPacket(opcode byte, path string, offset uint64, reqLen uint32, data []byte) []byte {
	buf := new(bytes.Buffer)
	dataLen := uint32(len(data))
	if opcode == OP_READ { dataLen = reqLen }

	// 🔥 LÕI V6.0: Ghép Client_ID (32-bit cao) và Req_ID (32-bit thấp) thành Session_ID 64-bit
	reqIdx := atomic.AddUint64(&globalReqId, 1) & 0xFFFFFFFF
	sessionID := (uint64(globalClientId) << 32) | uint64(reqIdx)

	binary.Write(buf, binary.LittleEndian, uint32(0x5A484941))
	binary.Write(buf, binary.LittleEndian, opcode)
	binary.Write(buf, binary.LittleEndian, sessionID)
	binary.Write(buf, binary.LittleEndian, offset)
	binary.Write(buf, binary.LittleEndian, dataLen)
	binary.Write(buf, binary.LittleEndian, uint16(len(path)))

	buf.WriteString(path)
	if data != nil { buf.Write(data) }
	return buf.Bytes()
}

func SendRpcVfs(opcode byte, path string, offset uint64, reqLen uint32, data []byte) ([]byte, error) {
	payload := BuildVfsPacket(opcode, path, offset, reqLen, data)
	cPayload := (*C.uint8_t)(C.CBytes(payload))
	defer C.free(unsafe.Pointer(cPayload))

	var outLen C.size_t
	cRes := C.zhiauth_send_vfs_command(cPayload, C.size_t(len(payload)), &outLen)

	if cRes == nil { return nil, fmt.Errorf("VFS RPC Timeout") }
	if outLen < 27 {
		C.free(unsafe.Pointer(cRes))
		return nil, fmt.Errorf("VFS RPC Invalid Size")
	}
	defer C.free(unsafe.Pointer(cRes))

	resBytes := C.GoBytes(unsafe.Pointer(cRes), C.int(outLen))
	if resBytes[4] == OP_ERROR { return nil, fmt.Errorf("Server VFS Error") }
	
	return resBytes[27:], nil 
}