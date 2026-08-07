//go:build windows
package main

import (
	"encoding/binary"
	"log"
	"strings"
	"sync"
	"time"

	"github.com/winfsp/cgofuse/fuse"
)

type CachedStat struct {
	Size      uint64
	IsDir     bool
	Exists    bool
	Timestamp time.Time
	SysTime   fuse.Timespec
	CTime     fuse.Timespec
	ATime     fuse.Timespec
}

type ZhiAuthFuse struct {
	fuse.FileSystemBase
	remoteBase string
	protocol   string
	tunnel     *QuicTunnel
	statCache  sync.Map
}

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
		data:        make([]byte, 0, 4194304), // Cấp trước 4MB RAM
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
			toSendLen = 131072 
		}

		chunk := make([]byte, toSendLen)
		copy(chunk, wb.data[:toSendLen])
		currentOffset := wb.startOffset

		wb.startOffset += uint64(toSendLen)
		wb.data = wb.data[toSendLen:]
		wb.mu.Unlock()

		_, err := f.callVfs(OP_WRITE, wb.realPath, currentOffset, uint32(toSendLen), chunk)
		if err != nil {
			log.Printf("[WRITE-ERR] ❌ Lỗi xả RAM KCP offset %d: %v", currentOffset, err)
		}
	}
}

func (f *ZhiAuthFuse) getVfsPath(reqPath string) string {
	p := strings.ReplaceAll(reqPath, "\\", "/")
	if p == "" || !strings.HasPrefix(p, "/") {
		p = "/" + p
	}
	return strings.TrimSuffix(f.remoteBase, "/") + p
}

func (f *ZhiAuthFuse) callVfs(opcode byte, realPath string, offset uint64, reqLen uint32, dataPayload []byte) ([]byte, error) {
	if f.protocol == "KCP" {
		return SendRpcVfs(opcode, realPath, offset, reqLen, dataPayload)
	}
	rawPacket := BuildVfsPacket(opcode, realPath, offset, reqLen, dataPayload)
	return f.tunnel.SendFsCommandRaw(rawPacket)
}

func (f *ZhiAuthFuse) clearStatCache(realPath string) {
	f.statCache.Delete(strings.ToLower(realPath))
}

func isADS(path string) bool { return strings.Contains(path, ":") }

func isWindowsSystemProbe(path string) bool {
	l := strings.ToLower(path)
	if strings.Contains(l, "desktop.ini") || strings.Contains(l, "autorun.inf") || strings.Contains(l, "thumbs.db") || strings.Contains(l, "folder.jpg") || strings.Contains(l, "folder.ico") {
		return true
	}
	return false
}

// =========================================================================
// 🔥 HỨNG PARSE BINARY STAT (37 BYTES) CỦA SERVER MỚI
// =========================================================================
func (f *ZhiAuthFuse) Getattr(path string, stat *fuse.Stat_t, fh uint64) int {
	if isADS(path) || isWindowsSystemProbe(path) {
		return -fuse.ENOENT
	}
	uid, gid, _ := fuse.Getcontext()
	realPath := f.getVfsPath(path)
	cacheKey := strings.ToLower(realPath)

	var pendingSize uint64 = 0
	if val, ok := writeBufferMap.Load(cacheKey); ok {
		wb := val.(*WriteBuffer)
		wb.mu.Lock()
		pendingSize = wb.startOffset + uint64(len(wb.data))
		wb.mu.Unlock()
	}

	if val, ok := f.statCache.Load(cacheKey); ok {
		item := val.(CachedStat)
		if time.Since(item.Timestamp) < 2000*time.Millisecond {
			if !item.Exists {
				return -fuse.ENOENT
			}
			stat.Uid, stat.Gid = uid, gid
			stat.Atim, stat.Mtim, stat.Ctim = item.ATime, item.SysTime, item.CTime
			
			finalSize := item.Size
			if pendingSize > finalSize { finalSize = pendingSize }

			if item.IsDir {
				stat.Mode = fuse.S_IFDIR | 0777
			} else {
				stat.Mode = fuse.S_IFREG | 0777
				stat.Size = int64(finalSize)
			}
			return 0
		}
	}

	res, err := f.callVfs(OP_STAT, realPath, 0, 0, nil)
	if err != nil || len(res) < 9 {
		f.statCache.Store(cacheKey, CachedStat{Exists: false, Timestamp: time.Now()})
		return -fuse.ENOENT
	}

	// Đọc Format chuẩn 37 bytes từ Server: [Size:8] [IsDir:1] [Mtime:8] [Ctime:8] [Atime:8] [Mode:4]
	size := binary.LittleEndian.Uint64(res[0:8])
	isDir := res[8] == 1

	mtime := uint64(time.Now().Unix())
	ctime := mtime
	atime := mtime

	if len(res) >= 37 {
		mtime = binary.LittleEndian.Uint64(res[9:17])
		ctime = binary.LittleEndian.Uint64(res[17:25])
		atime = binary.LittleEndian.Uint64(res[25:33])
	}

	mtmsp := fuse.NewTimespec(time.Unix(int64(mtime), 0))
	ctmsp := fuse.NewTimespec(time.Unix(int64(ctime), 0))
	atmsp := fuse.NewTimespec(time.Unix(int64(atime), 0))

	f.statCache.Store(cacheKey, CachedStat{
		Size:      size,
		IsDir:     isDir,
		Exists:    true,
		Timestamp: time.Now(),
		SysTime:   mtmsp,
		CTime:     ctmsp,
		ATime:     atmsp,
	})

	stat.Uid, stat.Gid = uid, gid
	stat.Atim, stat.Mtim, stat.Ctim = atmsp, mtmsp, ctmsp
	
	if pendingSize > size { size = pendingSize }

	if isDir {
		stat.Mode = fuse.S_IFDIR | 0777
	} else {
		stat.Mode = fuse.S_IFREG | 0777
		stat.Size = int64(size)
	}
	return 0
}

func (f *ZhiAuthFuse) Access(path string, mask uint32) int {
	if isADS(path) || isWindowsSystemProbe(path) {
		return -fuse.ENOENT
	}
	return 0
}
func (f *ZhiAuthFuse) Statfs(path string, stat *fuse.Statfs_t) int {
	stat.Bsize = 4096
	stat.Frsize = 4096
	stat.Blocks = 268435456
	stat.Bfree = 134217728
	stat.Bavail = 134217728
	return 0
}
func (f *ZhiAuthFuse) Create(path string, flags int, mode uint32) (int, uint64) {
	if isADS(path) { return -fuse.ENOENT, ^uint64(0) }
	if isWindowsSystemProbe(path) { return -fuse.ENOENT, ^uint64(0) }
	ts := time.Now().Format("15:04:05.000")
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	log.Printf("[%s] [WINFSP-%s] 🆕 CREATE FILE: %s", ts, f.protocol, realPath)
	_, err := f.callVfs(OP_WRITE, realPath, 0, 0, []byte{})
	if err != nil {
		return -fuse.EIO, ^uint64(0)
	}
	return 0, 1
}
func (f *ZhiAuthFuse) Open(path string, flags int) (int, uint64) {
	if isADS(path) { return -fuse.ENOENT, ^uint64(0) }
	if isWindowsSystemProbe(path) { return -fuse.ENOENT, ^uint64(0) }
	return 0, 1
}
func (f *ZhiAuthFuse) Opendir(path string) (int, uint64) { return 0, 1 }

// =========================================================================
// 🔥 HỨNG PARSE BINARY LIST (15 BYTES HEADER) TỪ SERVER MỚI
// =========================================================================
func (f *ZhiAuthFuse) Readdir(path string, fill func(name string, stat *fuse.Stat_t, ofst int64) bool, ofst int64, fh uint64) int {
	realPath := f.getVfsPath(path)
	fill(".", nil, 0)
	fill("..", nil, 0)

	payload, err := f.callVfs(OP_LIST, realPath, 0, 0, nil)
	if err != nil || len(payload) == 0 {
		return 0
	}

	uid, gid, _ := fuse.Getcontext()
	sysTmsp := fuse.NewTimespec(time.Now())

	offset := 0
	totalLen := len(payload)

	for offset+15 <= totalLen {
		// Trình tự Header 15 bytes: [NameLen: 2] [IsDir: 1] [Size: 8] [Mode: 4]
		nameLen := binary.LittleEndian.Uint16(payload[offset : offset+2])
		isDir := payload[offset+2] == 1
		fileSize := binary.LittleEndian.Uint64(payload[offset+3 : offset+11])

		offset += 15 

		if offset+int(nameLen) > totalLen {
			break
		}

		name := string(payload[offset : offset+int(nameLen)])
		offset += int(nameLen) 

		if isADS(name) || isWindowsSystemProbe(name) {
			continue
		}

		var st fuse.Stat_t
		st.Uid, st.Gid = uid, gid
		st.Atim, st.Mtim, st.Ctim = sysTmsp, sysTmsp, sysTmsp

		if isDir {
			st.Mode = fuse.S_IFDIR | 0777
		} else {
			st.Mode = fuse.S_IFREG | 0777
			st.Size = int64(fileSize)
		}

		fill(name, &st, 0)

		childPath := realPath
		if !strings.HasSuffix(childPath, "/") {
			childPath += "/"
		}
		childPath += name
		cacheKey := strings.ToLower(childPath)

		f.statCache.Store(cacheKey, CachedStat{
			Size:      fileSize,
			IsDir:     isDir,
			Exists:    true,
			Timestamp: time.Now(),
			SysTime:   sysTmsp,
			CTime:     sysTmsp,
			ATime:     sysTmsp,
		})
	}
	return 0
}

func (f *ZhiAuthFuse) Read(path string, buff []byte, ofst int64, fh uint64) int {
	if isADS(path) { return -fuse.ENOENT }
	realPath := f.getVfsPath(path)
	totalReq := len(buff)
	if totalReq == 0 { return 0 }

	if strings.HasSuffix(strings.ToLower(realPath), ".zip") && uint32(totalReq) < 131072 {
		return 0
	}

	if f.protocol == "QUIC" {
		chunk, err := f.callVfs(OP_READ, realPath, uint64(ofst), uint32(totalReq), nil)
		if err != nil || len(chunk) == 0 {
			return 0
		}
		return copy(buff, chunk)
	}

	var safeChunkSize uint32 = 131072
	if uint32(totalReq) <= safeChunkSize {
		chunk, err := f.callVfs(OP_READ, realPath, uint64(ofst), uint32(totalReq), nil)
		if err != nil || len(chunk) == 0 {
			return 0
		}
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
			if err != nil || len(chunk) == 0 {
				return
			}

			copy(buff[chunkIdx*safeChunkSize:], chunk)

			mu.Lock()
			totalBytesRead += uint32(len(chunk))
			mu.Unlock()
		}(i)
	}

	wg.Wait()
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
		_, err := f.callVfs(OP_WRITE, realPath, uint64(ofst), uint32(totalLen), buff)
		if err != nil { return -fuse.EIO }
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

func (f *ZhiAuthFuse) Fsync(path string, datasync bool, fh uint64) int {
	return f.Flush(path, fh)
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
	if isADS(oldpath) || isADS(newpath) { return -fuse.ENOENT }
	realOldPath := f.getVfsPath(oldpath)
	realNewPath := f.getVfsPath(newpath)

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

	newKey := strings.ToLower(realNewPath)
	writeBufferMap.Delete(newKey)

	f.clearStatCache(realOldPath)
	f.clearStatCache(realNewPath)
	_, err := f.callVfs(OP_RENAME, realOldPath, 0, 0, []byte(realNewPath))
	if err != nil {
		return -fuse.EIO
	}
	return 0
}

func (f *ZhiAuthFuse) Unlink(path string) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	key := strings.ToLower(realPath)
	writeBufferMap.Delete(key)

	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_DELETE, realPath, 0, 0, nil)
	if err != nil {
		return -fuse.EIO
	}
	return 0
}

func (f *ZhiAuthFuse) Truncate(path string, size int64, fh uint64) int {
	if isADS(path) { return -fuse.ENOENT }
	realPath := f.getVfsPath(path)
	key := strings.ToLower(realPath)
	
	writeBufferMap.Delete(key)
	f.clearStatCache(realPath)
	
	_, err := f.callVfs(OP_TRUNCATE, realPath, uint64(size), 0, nil)
	if err != nil {
		return -fuse.EIO
	}
	return 0
}

func (f *ZhiAuthFuse) Rmdir(path string) int {
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_DELETE, realPath, 0, 0, nil)
	if err != nil {
		return -fuse.EIO
	}
	return 0
}

func (f *ZhiAuthFuse) Mkdir(path string, mode uint32) int {
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_MKDIR, realPath, 0, 0, nil)
	if err != nil {
		return -fuse.EIO
	}
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

	options := []string{"-f", "-o", "file_system_name=ZhiAuth-" + protocol, "-o", "volname=ZhiAuth " + protocol, "-o", "allow_other", "-o", "ThreadCount=16"}
	go func() { host.Mount(mountPoint, options) }()
}