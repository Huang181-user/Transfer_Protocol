//go:build windows
package main

import (
	"encoding/binary"
	"log"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/winfsp/cgofuse/fuse"
)

type CachedStat struct { Size uint64; IsDir bool; Exists bool; Timestamp time.Time; SysTime fuse.Timespec }
type ZhiAuthFuse struct { fuse.FileSystemBase; remoteBase string; protocol string; tunnel *QuicTunnel; statCache sync.Map }

// =========================================================================
// 🔥 BỘ ĐỆM GHI RAM KHÔNG KHÓA (MUTEX-FREE ASYNC FLUSHER)
// =========================================================================
type WriteBuffer struct {
	mu          sync.Mutex
	realPath    string
	startOffset uint64
	data        []byte
	isFlushing  bool
}

var writeBufferMap sync.Map

func getOrCreateWriteBuffer(realPath string, initialOffset uint64) *WriteBuffer {
	key := strings.ToLower(realPath)
	if val, ok := writeBufferMap.Load(key); ok {
		return val.(*WriteBuffer)
	}
	wb := &WriteBuffer{
		realPath:    realPath,
		startOffset: initialOffset,
		data:        make([]byte, 0, 4194304), // Cấp trước 4MB RAM cho bốc
	}
	actual, _ := writeBufferMap.LoadOrStore(key, wb)
	return actual.(*WriteBuffer)
}

func (wb *WriteBuffer) FlushChunk(f *ZhiAuthFuse, forceAll bool) {
	for {
		wb.mu.Lock()
		if len(wb.data) == 0 || (!forceAll && len(wb.data) < 131072) {
			wb.isFlushing = false
			wb.mu.Unlock()
			return
		}

		toSendLen := len(wb.data)
		if toSendLen > 131072 {
			toSendLen = 131072 // Băm đúng 128KB để luồng UDP trơn tru
		}

		// Sao chép hàng ra xe thồ
		chunk := make([]byte, toSendLen)
		copy(chunk, wb.data[:toSendLen])
		currentOffset := wb.startOffset

		// Cập nhật sổ sách và TRẢ CHÌA KHÓA KHO NGAY LẬP TỨC!
		wb.startOffset += uint64(toSendLen)
		wb.data = wb.data[toSendLen:]
		wb.mu.Unlock() // 🔓 THÁO KHÓA! Chrome cứ việc ném hàng tiếp vào kho.

		// Thong thả giao hàng mạng KCP (Bị delay 15s cũng kệ, Chrome chả quan tâm)
		_, err := f.callVfs(OP_WRITE, wb.realPath, currentOffset, uint32(toSendLen), chunk)
		if err != nil {
			log.Printf("[WRITE-ERR] ❌ Lỗi xả RAM KCP offset %d: %v", currentOffset, err)
		}
	}
}

func (f *ZhiAuthFuse) getVfsPath(reqPath string) string {
	p := strings.ReplaceAll(reqPath, "\\", "/")
	if p == "" || !strings.HasPrefix(p, "/") { p = "/" + p }
	return strings.TrimSuffix(f.remoteBase, "/") + p
}

func (f *ZhiAuthFuse) callVfs(opcode byte, realPath string, offset uint64, reqLen uint32, dataPayload []byte) ([]byte, error) {
	if f.protocol == "KCP" { return SendRpcVfs(opcode, realPath, offset, reqLen, dataPayload) }
	rawPacket := BuildVfsPacket(opcode, realPath, offset, reqLen, dataPayload)
	return f.tunnel.SendFsCommandRaw(rawPacket)
}

func (f *ZhiAuthFuse) clearStatCache(realPath string) { f.statCache.Delete(strings.ToLower(realPath)) }
func isADS(path string) bool { return strings.Contains(path, ":") }

func isWindowsSystemProbe(path string) bool {
	l := strings.ToLower(path)
	if strings.Contains(l, "desktop.ini") || strings.Contains(l, "autorun.inf") || strings.Contains(l, "thumbs.db") || strings.Contains(l, "folder.jpg") || strings.Contains(l, "folder.ico") {
		return true
	}
	return false
}

func (f *ZhiAuthFuse) Getattr(path string, stat *fuse.Stat_t, fh uint64) int {
	if isADS(path) || isWindowsSystemProbe(path) { return -fuse.ENOENT }
	uid, gid, _ := fuse.Getcontext()
	realPath := f.getVfsPath(path)
	cacheKey := strings.ToLower(realPath)

	if val, ok := f.statCache.Load(cacheKey); ok {
		item := val.(CachedStat)
		if time.Since(item.Timestamp) < 10000*time.Millisecond {
			if !item.Exists { return -fuse.ENOENT }
			stat.Uid, stat.Gid = uid, gid
			stat.Atim, stat.Mtim, stat.Ctim = item.SysTime, item.SysTime, item.SysTime
			if item.IsDir { stat.Mode = fuse.S_IFDIR | 0777 } else { stat.Mode = fuse.S_IFREG | 0777; stat.Size = int64(item.Size) }
			return 0
		}
	}

	res, err := f.callVfs(OP_STAT, realPath, 0, 0, nil)
	if err != nil || len(res) < 9 {
		f.statCache.Store(cacheKey, CachedStat{Exists: false, Timestamp: time.Now()})
		return -fuse.ENOENT
	}

	size := binary.LittleEndian.Uint64(res[0:8])
	isDir := res[8] == 1

	var mtime uint64 = 0
	if len(res) >= 17 { mtime = binary.LittleEndian.Uint64(res[9:17]) }
	if mtime == 0 { mtime = uint64(time.Now().Unix()) }

	tmsp := fuse.NewTimespec(time.Unix(int64(mtime), 0))
	f.statCache.Store(cacheKey, CachedStat{Size: size, IsDir: isDir, Exists: true, Timestamp: time.Now(), SysTime: tmsp})

	stat.Uid, stat.Gid = uid, gid
	stat.Atim, stat.Mtim, stat.Ctim = tmsp, tmsp, tmsp
	if isDir { stat.Mode = fuse.S_IFDIR | 0777 } else { stat.Mode = fuse.S_IFREG | 0777; stat.Size = int64(size) }
	return 0
}

func (f *ZhiAuthFuse) Access(path string, mask uint32) int {
	if isADS(path) || isWindowsSystemProbe(path) { return -fuse.ENOENT }
	return 0
}
func (f *ZhiAuthFuse) Statfs(path string, stat *fuse.Statfs_t) int {
	stat.Bsize = 4096; stat.Frsize = 4096; stat.Blocks = 268435456; stat.Bfree = 134217728; stat.Bavail = 134217728; return 0
}
func (f *ZhiAuthFuse) Create(path string, flags int, mode uint32) (int, uint64) {
	if isADS(path) { return 0, 9999 }
	if isWindowsSystemProbe(path) { return -fuse.ENOENT, 0 }
	ts := time.Now().Format("2006-01-02 15:04:05.000")
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	log.Printf("[%s] [WINFSP-%s] 🆕 CREATE FILE: %s", ts, f.protocol, realPath)
	_, err := f.callVfs(OP_WRITE, realPath, 0, 0, []byte{})
	if err != nil { log.Printf("[%s] ❌ [WINFSP-CREATE-ERR] %v", ts, err); return -fuse.EIO, 0 }
	return 0, 1
}
func (f *ZhiAuthFuse) Open(path string, flags int) (int, uint64) {
	if isADS(path) { return 0, 9999 }
	if isWindowsSystemProbe(path) { return -fuse.ENOENT, 0 }
	return 0, 1
}
func (f *ZhiAuthFuse) Opendir(path string) (int, uint64) { return 0, 1 }

func (f *ZhiAuthFuse) Readdir(path string, fill func(name string, stat *fuse.Stat_t, ofst int64) bool, ofst int64, fh uint64) int {
	realPath := f.getVfsPath(path)
	fill(".", nil, 0)
	fill("..", nil, 0)

	payload, err := f.callVfs(OP_LIST, realPath, 0, 0, nil)
	if err != nil || len(payload) == 0 { return 0 }

	uid, gid, _ := fuse.Getcontext()
	tokens := strings.Split(string(payload), "|")
	
	currentTime := time.Now()
	sysTmsp := fuse.NewTimespec(currentTime)

	for _, token := range tokens {
		if token == "" { continue }
		parts := strings.Split(token, ",")
		if len(parts) < 2 { continue }
		
		name := parts[0]
		isDir := parts[1] == "DIR"
		if isADS(name) || isWindowsSystemProbe(name) { continue }

		var st fuse.Stat_t
		st.Uid, st.Gid = uid, gid
		st.Atim, st.Mtim, st.Ctim = sysTmsp, sysTmsp, sysTmsp

		var fileSize uint64 = 0
		if len(parts) >= 3 {
			if parsed, err := strconv.ParseUint(parts[2], 10, 64); err == nil {
				fileSize = parsed
			}
		}

		if isDir {
			st.Mode = fuse.S_IFDIR | 0777
		} else {
			st.Mode = fuse.S_IFREG | 0777
			st.Size = int64(fileSize)
		}

		fill(name, &st, 0)

		childPath := realPath
		if !strings.HasSuffix(childPath, "/") { childPath += "/" }
		childPath += name
		cacheKey := strings.ToLower(childPath)

		f.statCache.Store(cacheKey, CachedStat{
			Size:      fileSize,
			IsDir:     isDir,
			Exists:    true,
			Timestamp: currentTime,
			SysTime:   sysTmsp,
		})
	}
	return 0
}

func (f *ZhiAuthFuse) Read(path string, buff []byte, ofst int64, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	totalReq := len(buff)
	if totalReq == 0 { return 0 }

	if strings.HasSuffix(strings.ToLower(realPath), ".zip") && uint32(totalReq) < 131072 { return 0 }

	if f.protocol == "QUIC" {
		chunk, err := f.callVfs(OP_READ, realPath, uint64(ofst), uint32(totalReq), nil)
		if err != nil || len(chunk) == 0 { return 0 }
		return copy(buff, chunk)
	}

	// =========================================================================
	// 🔥 TỐI ƯU HÓA KCP: KHÔI PHỤC KÉO ĐA LUỒNG SONG SONG (0.33 GIÂY)
	// =========================================================================
	var safeChunkSize uint32 = 131072 // Băm 128KB mỗi luồng
	if uint32(totalReq) <= safeChunkSize {
		chunk, err := f.callVfs(OP_READ, realPath, uint64(ofst), uint32(totalReq), nil)
		if err != nil || len(chunk) == 0 { return 0 }
		return copy(buff, chunk)
	}

	numChunks := (uint32(totalReq) + safeChunkSize - 1) / safeChunkSize
	var wg sync.WaitGroup
	var totalBytesRead uint32 = 0
	var mu sync.Mutex

	for i := uint32(0); i < numChunks; i++ {
		wg.Add(1)
		go func(chunkIdx uint32) {
			defer wg.Done()
			currentOffset := uint64(ofst) + uint64(chunkIdx*safeChunkSize)
			fetchSize := safeChunkSize
			if chunkIdx == numChunks-1 {
				fetchSize = uint32(totalReq) - (chunkIdx * safeChunkSize)
			}
			chunk, err := f.callVfs(OP_READ, realPath, currentOffset, fetchSize, nil)
			if err != nil || len(chunk) == 0 { return }

			copy(buff[chunkIdx*safeChunkSize:], chunk)
			mu.Lock()
			totalBytesRead += uint32(len(chunk))
			mu.Unlock()
		}(i)
	}

	wg.Wait()
	log.Printf("[%s] [DEBUG-READ] ✅ TỐC ĐỘ BÀN THỜ! Đã gom xong %d bytes (Bằng %d luồng song song).", time.Now().Format("15:04:05.000"), totalBytesRead, numChunks)
	return int(totalBytesRead)
}

func (f *ZhiAuthFuse) Write(path string, buff []byte, ofst int64, fh uint64) int {
	if isADS(path) { return len(buff) }
	realPath := f.getVfsPath(path)
	totalLen := len(buff)

	if totalLen == 0 {
		f.clearStatCache(realPath)
		f.callVfs(OP_WRITE, realPath, uint64(ofst), 0, []byte{})
		return 0
	}

	if f.protocol == "QUIC" {
		f.clearStatCache(realPath)
		f.callVfs(OP_WRITE, realPath, uint64(ofst), uint32(totalLen), buff)
		return totalLen
	}

	f.clearStatCache(realPath)
	wb := getOrCreateWriteBuffer(realPath, uint64(ofst))
	
	wb.mu.Lock()
	wb.data = append(wb.data, buff...)
	shouldFlush := false
	if len(wb.data) >= 131072 && !wb.isFlushing {
		wb.isFlushing = true
		shouldFlush = true
	}
	wb.mu.Unlock()

	if shouldFlush {
		go wb.FlushChunk(f, false)
	}

	return totalLen
}

func (f *ZhiAuthFuse) Flush(path string, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	key := strings.ToLower(realPath)
	if val, ok := writeBufferMap.Load(key); ok {
		wb := val.(*WriteBuffer)
		wb.mu.Lock()
		if !wb.isFlushing {
			wb.isFlushing = true
			wb.mu.Unlock()
			wb.FlushChunk(f, true)
		} else {
			// Đợi luồng xả ngầm làm xong nốt mẻ cuối cùng
			for len(wb.data) > 0 {
				wb.mu.Unlock()
				time.Sleep(10 * time.Millisecond)
				wb.mu.Lock()
			}
			wb.mu.Unlock()
		}
	}
	return 0
}

func (f *ZhiAuthFuse) Release(path string, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	key := strings.ToLower(realPath)
	if val, ok := writeBufferMap.Load(key); ok {
		wb := val.(*WriteBuffer)
		wb.mu.Lock()
		if !wb.isFlushing {
			wb.isFlushing = true
			wb.mu.Unlock()
			wb.FlushChunk(f, true)
		} else {
			for len(wb.data) > 0 {
				wb.mu.Unlock()
				time.Sleep(10 * time.Millisecond)
				wb.mu.Lock()
			}
			wb.mu.Unlock()
		}
		writeBufferMap.Delete(key)
	}
	return 0
}

func (f *ZhiAuthFuse) Rename(oldpath string, newpath string) int {
	if isADS(oldpath) || isADS(newpath) { return 0 }
	realOldPath := f.getVfsPath(oldpath)
	realNewPath := f.getVfsPath(newpath)

	// Chốt chặn an toàn: Xả cạn hàng trước khi đổi tên file tạm .crdownload
	oldKey := strings.ToLower(realOldPath)
	if val, ok := writeBufferMap.Load(oldKey); ok {
		wb := val.(*WriteBuffer)
		wb.mu.Lock()
		if !wb.isFlushing {
			wb.isFlushing = true
			wb.mu.Unlock()
			wb.FlushChunk(f, true)
		} else {
			for len(wb.data) > 0 {
				wb.mu.Unlock()
				time.Sleep(10 * time.Millisecond)
				wb.mu.Lock()
			}
			wb.mu.Unlock()
		}
		writeBufferMap.Delete(oldKey)
	}

	f.clearStatCache(realOldPath)
	f.clearStatCache(realNewPath)
	ts := time.Now().Format("15:04:05.000")
	log.Printf("[%s] [WINFSP-%s] 🚚 RENAME: %s -> %s", ts, f.protocol, realOldPath, realNewPath)
	_, err := f.callVfs(OP_RENAME, realOldPath, 0, 0, []byte(realNewPath))
	if err != nil { return -fuse.EIO }
	return 0
}

func (f *ZhiAuthFuse) Unlink(path string) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	key := strings.ToLower(realPath)
	writeBufferMap.Delete(key)

	f.clearStatCache(realPath)
	ts := time.Now().Format("15:04:05.000")
	log.Printf("[%s] [WINFSP-%s] 🗑️ UNLINK FILE: %s", ts, f.protocol, realPath)
	_, err := f.callVfs(OP_DELETE, realPath, 0, 0, nil)
	if err != nil { return -fuse.EIO }
	return 0
}

func (f *ZhiAuthFuse) Truncate(path string, size int64, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_TRUNCATE, realPath, uint64(size), 0, nil)
	if err != nil { return -fuse.EIO }
	return 0
}

func (f *ZhiAuthFuse) Rmdir(path string) int {
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_DELETE, realPath, 0, 0, nil)
	if err != nil { return -fuse.EIO }
	return 0
}

func (f *ZhiAuthFuse) Mkdir(path string, mode uint32) int {
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_MKDIR, realPath, 0, 0, nil)
	if err != nil { return -fuse.EIO }
	return 0
}

func (f *ZhiAuthFuse) Utimens(path string, tmsp []fuse.Timespec) int { return 0 }
func (f *ZhiAuthFuse) Chmod(path string, mode uint32) int { return 0 }
func (f *ZhiAuthFuse) Chown(path string, uid uint32, gid uint32) int { return 0 }

func MountWinFspDrive(driveLetter string, remoteBase string, tunnel *QuicTunnel, protocol string) {
	fsImpl := &ZhiAuthFuse{remoteBase: remoteBase, protocol: protocol, tunnel: tunnel}
	host := fuse.NewFileSystemHost(fsImpl)
	host.SetCapReaddirPlus(false)
	mountPoint := driveLetter + ":"
	
	options := []string{"-f", "-o", "file_system_name=ZhiAuth-" + protocol, "-o", "volname=ZhiAuth " + protocol, "-o", "allow_other", "-o", "ThreadCount=16", "-o", "FileInfoTimeout=10000", "-o", "DirInfoTimeout=10000"}
	go func() { host.Mount(mountPoint, options) }()
}