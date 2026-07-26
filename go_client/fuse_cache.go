package main

import (
	"log"
	"strconv"
	"strings"
	"sync"
	"time"
)

type FileMeta struct {
	IsDir bool
	Size  uint64
	Mode  uint32
	Exp   time.Time
}

// 🛡️ BẢO BỐI: Bản đồ RAM an toàn cho hệ đa luồng (Thread-safe Map)
var ramCache = struct {
	sync.RWMutex
	m map[string]FileMeta
}{m: make(map[string]FileMeta)}

func InjectBulkCache(parentPath string, bulkData string) {
	ramCache.Lock()
	defer ramCache.Unlock()

	log.Printf("[RAM-CACHE] ⚡ Mở khóa đa luồng: Bơm hàng loạt Metadata vào RAM cho thư mục: [%s]", parentPath)
	count := 0

	tokens := strings.Split(bulkData, "|")
	for _, token := range tokens {
		if token == "" {
			continue
		}
		parts := strings.Split(token, ",")
		if len(parts) < 4 {
			continue
		}

		name := parts[0]
		isDir := parts[1] == "DIR"
		size, _ := strconv.ParseUint(parts[2], 10, 64)
		mode64, _ := strconv.ParseUint(parts[3], 10, 32)

		fullPath := parentPath + "/" + name
		if parentPath == "/" {
			fullPath = "/" + name
		} // Xử lý méo dấu slash

		ramCache.m[fullPath] = FileMeta{
			IsDir: isDir,
			Size:  size,
			Mode:  uint32(mode64),
			Exp:   time.Now().Add(60 * time.Second),
		}
		count++
	}
	log.Printf("[RAM-CACHE] 🎯 Đã nén thành công %d Object vào bộ nhớ đệm nội bộ (Zero-Latency)!", count)
}

func CheckRAMCache(fullPath string) (FileMeta, bool) {
	ramCache.RLock()
	defer ramCache.RUnlock()

	meta, exists := ramCache.m[fullPath]
	if exists && time.Now().Before(meta.Exp) {
		log.Printf("[CACHE-HIT] 🚀 Tốc độ ánh sáng! Truy xuất trực tiếp từ RAM không qua mạng: %s", fullPath)
		return meta, true
	}
	return FileMeta{}, false
}

// Thêm hàm này vào cuối file fuse_cache.go
func RemoveFromRAMCache(fullPath string) {
	ramCache.Lock()
	defer ramCache.Unlock()

	if _, exists := ramCache.m[fullPath]; exists {
		delete(ramCache.m, fullPath)
		log.Printf("[CACHE-CLEAR] 🧹 Đã quét sạch tàn dư bóng ma trong RAM: %s", fullPath)
	}
}
