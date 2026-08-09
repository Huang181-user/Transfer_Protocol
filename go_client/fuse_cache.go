package main

import (
	"encoding/binary"
	"log"
	"sync"
	"time"
)

type FileMeta struct {
	IsDir bool
	Size  uint64
	Mode  uint32
	Exp   time.Time
}

// 🛡️ BẢO BỐI: Bản đồ RAM an toàn cho hệ đa luồng
var ramCache = struct {
	sync.RWMutex
	m map[string]FileMeta
}{m: make(map[string]FileMeta)}

// NHẬN TRỰC TIẾP MẢNG BYTE TỪ C++
func InjectBulkCache(parentPath string, bulkData []byte) {
	ramCache.Lock()
	defer ramCache.Unlock()

	offset := 0
	totalLen := len(bulkData)

	// Quét qua mảng byte cho đến khi hết dữ liệu
	for offset+15 <= totalLen {
		// Bóc 15 bytes Header
		nameLen := int(binary.LittleEndian.Uint16(bulkData[offset : offset+2]))
		isDirVal := bulkData[offset+2]
		size := binary.LittleEndian.Uint64(bulkData[offset+3 : offset+11])
		mode64 := binary.LittleEndian.Uint32(bulkData[offset+11 : offset+15])

		offset += 15

		// Check an toàn chống lỗi tràn bộ nhớ (Out of bounds)
		if offset+nameLen > totalLen {
			log.Println("[CACHE-WARNING] Cảnh báo: Gói tin thư mục bị cắt xén!")
			break
		}

		// Bóc Tên file theo độ dài NameLen
		name := string(bulkData[offset : offset+nameLen])
		offset += nameLen

		isDir := isDirVal == 1
		fullPath := parentPath + "/" + name
		if parentPath == "/" {
			fullPath = "/" + name
		}

		ramCache.m[fullPath] = FileMeta{
			IsDir: isDir,
			Size:  size,
			Mode:  mode64,
			Exp:   time.Now().Add(60 * time.Second),
		}
	}
}

func CheckRAMCache(fullPath string) (FileMeta, bool) {
	ramCache.RLock()
	defer ramCache.RUnlock()

	meta, exists := ramCache.m[fullPath]
	if exists && time.Now().Before(meta.Exp) {
		log.Printf("[CACHE-HIT] 🚀 Tốc độ ánh sáng! Truy xuất trực tiếp từ RAM: %s", fullPath)
		return meta, true
	}
	return FileMeta{}, false
}

func RemoveFromRAMCache(fullPath string) {
	ramCache.Lock()
	defer ramCache.Unlock()

	if _, exists := ramCache.m[fullPath]; exists {
		delete(ramCache.m, fullPath)
		log.Printf("[CACHE-CLEAR] 🧹 Đã quét sạch tàn dư bóng ma trong RAM: %s", fullPath)
	}
}
