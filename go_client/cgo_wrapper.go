package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#cgo LDFLAGS: -L../build -lzhiauth_client_core -lstdc++ -lsodium -lpthread
#include "client_bridge.h"
#include <stdlib.h>
*/
import "C"
import (
	"bytes"
	"encoding/binary"
	"fmt"
	"log"
	"sync"
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

var globalReqId uint32 = 0
var pendingRequests sync.Map
var appClientID uint32 = uint32(time.Now().UnixNano() & 0xFFFFFFFF)

// Sửa hàm InitCppSDK trong go_client/cgo_wrapper.go
func InitCppSDK(ip string, port int, symKey string, mtu int, tuning KcpTuningParams) bool {
	cIP, cKey := C.CString(ip), C.CString(symKey)
	defer C.free(unsafe.Pointer(cIP))
	defer C.free(unsafe.Pointer(cKey))

	// Nếu Server cấp động -> Dùng tuning từ Server. Ngược lại -> Dùng mặc định Wi-Fi Safe Mode (1, 10, 2, 0, 512, 512)
	noDelay, interval, resend, nc, sndWnd, rcvWnd := 1, 10, 2, 0, 512, 512
	if tuning.IsDynamic {
		noDelay = tuning.NoDelay
		interval = tuning.Interval
		resend = tuning.Resend
		nc = tuning.Nc
		sndWnd = tuning.SndWnd
		rcvWnd = tuning.RcvWnd
	}

	return C.zhiauth_client_init(
		cIP, C.int(port), cKey, C.int(mtu),
		C.int(noDelay), C.int(interval), C.int(resend), C.int(nc),
		C.int(sndWnd), C.int(rcvWnd),
	) == 0
}

func ShutdownCppSDK() { C.zhiauth_client_shutdown() }

//export zhiauth_cgo_on_response
func zhiauth_cgo_on_response(reqId C.uint64_t, data *C.uint8_t, length C.size_t) {
	goReqId := uint32(uint64(reqId) & 0xFFFFFFFF)
	if val, ok := pendingRequests.Load(goReqId); ok {
		ch := val.(chan []byte)
		safeData := C.GoBytes(unsafe.Pointer(data), C.int(length))
		select {
		case ch <- safeData:
		default:
		}
	}
}

func BuildVfsPacket(opcode byte, path string, offset uint64, reqLen uint32, data []byte) ([]byte, uint32) {
	buf := new(bytes.Buffer)
	dataLen := uint32(len(data))
	if opcode == OP_READ {
		dataLen = reqLen
	}

	reqId := atomic.AddUint32(&globalReqId, 1)
	combinedSessionId := (uint64(appClientID) << 32) | uint64(reqId)

	binary.Write(buf, binary.LittleEndian, uint32(0x5A484941))
	binary.Write(buf, binary.LittleEndian, opcode)
	binary.Write(buf, binary.LittleEndian, combinedSessionId)
	binary.Write(buf, binary.LittleEndian, offset)
	binary.Write(buf, binary.LittleEndian, dataLen)
	binary.Write(buf, binary.LittleEndian, uint16(len(path)))

	buf.WriteString(path)
	if data != nil {
		buf.Write(data)
	}
	return buf.Bytes(), reqId
}

func SendRpcVfs(opcode byte, path string, offset uint64, reqLen uint32, data []byte) ([]byte, error) {
	startTrace := time.Now()
	payload, reqId := BuildVfsPacket(opcode, path, offset, reqLen, data)

	resChan := make(chan []byte, 1)
	pendingRequests.Store(reqId, resChan)
	defer pendingRequests.Delete(reqId)

	// 🔥 IN LOG ĐỂ THEO DÕI XEM NÓ CÓ THOÁT KHỎI ĐƯỢC BƯỚC GỬI KHÔNG
	log.Printf("[%s] [PROFILER-GO] 📤 Bơm Request [ID: %d] [OP: %d] xuống tầng C++... (Kích thước: %d bytes)", time.Now().Format("2006-01-02 15:04:05.000"), reqId, opcode, len(payload))

	cPayload := (*C.uint8_t)(C.CBytes(payload))
	C.zhiauth_send_vfs_command_async(cPayload, C.size_t(len(payload)))
	C.free(unsafe.Pointer(cPayload))

	select {
	case resBytes := <-resChan:
		duration := time.Since(startTrace).Milliseconds()
		// 🔥 IN LOG THEO DÕI TỔNG THỜI GIAN NHẬN PHẢN HỒI
		log.Printf("[%s] [PROFILER-GO] 📥 Nhận phản hồi [ID: %d] thành công! Mất: %d ms", time.Now().Format("2006-01-02 15:04:05.000"), reqId, duration)

		if len(resBytes) < 27 {
			return nil, fmt.Errorf("VFS Packet Corrupted")
		}
		if resBytes[4] == OP_ERROR {
			return nil, fmt.Errorf("Server VFS Error")
		}
		return resBytes[27:], nil

	case <-time.After(30 * time.Second):
		log.Printf("[%s] [PROFILER-GO] 💥 MẤT TÍN HIỆU! Bị kẹt sau 15s chờ đợi tại Request [ID: %d] [OP: %d] [Path: %s]", time.Now().Format("2006-01-02 15:04:05.000"), reqId, opcode, path)
		return nil, fmt.Errorf("VFS RPC Timeout")
	}
}
