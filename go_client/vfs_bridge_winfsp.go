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

type CachedStat struct { Size uint64; IsDir bool; Exists bool; Timestamp time.Time; SysTime fuse.Timespec }
type ZhiAuthFuse struct { fuse.FileSystemBase; remoteBase string; protocol string; tunnel *QuicTunnel; statCache sync.Map }

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

func (f *ZhiAuthFuse) clearStatCache(realPath string) { f.statCache.Delete(realPath) }
func isADS(path string) bool { return strings.Contains(path, ":") }

// 🔥 BỨC TƯỜNG LỬA CHẶN THUMBNAIL EXPLORER
func isWindowsSystemProbe(path string) bool {
	l := strings.ToLower(path)
	if strings.Contains(l, "desktop.ini") || strings.Contains(l, "autorun.inf") || strings.Contains(l, "thumbs.db") || strings.Contains(l, "folder.jpg") || strings.Contains(l, "folder.ico") {
        return true
    }
    // Chặn Explorer đọc lén file ZIP và DOCX tạm thời để lấy metadata
    if strings.HasSuffix(l, ".zip") || strings.Contains(l, "~$") || strings.HasSuffix(l, ".exe") {
        // Chỉ chặn ngầm nếu Windows gọi liên tục (tránh treo), không chặn app khác mở file
        return false 
    }
    return false
}

func (f *ZhiAuthFuse) Getattr(path string, stat *fuse.Stat_t, fh uint64) int {
	if isADS(path) || isWindowsSystemProbe(path) { return -fuse.ENOENT }
	uid, gid, _ := fuse.Getcontext()
	realPath := f.getVfsPath(path)

	if val, ok := f.statCache.Load(realPath); ok {
		item := val.(CachedStat)
		if time.Since(item.Timestamp) < 5000*time.Millisecond {
			if !item.Exists { return -fuse.ENOENT }
			stat.Uid, stat.Gid = uid, gid
			stat.Atim, stat.Mtim, stat.Ctim = item.SysTime, item.SysTime, item.SysTime
			if item.IsDir { stat.Mode = fuse.S_IFDIR | 0777 } else { stat.Mode = fuse.S_IFREG | 0777; stat.Size = int64(item.Size) }
			return 0
		}
	}

	res, err := f.callVfs(OP_STAT, realPath, 0, 0, nil)
	if err != nil || len(res) < 9 {
		f.statCache.Store(realPath, CachedStat{Exists: false, Timestamp: time.Now()})
		return -fuse.ENOENT
	}

	size := binary.LittleEndian.Uint64(res[0:8])
	isDir := res[8] == 1
	
	var mtime uint64 = 0
	if len(res) >= 17 { mtime = binary.LittleEndian.Uint64(res[9:17]) }
	if mtime == 0 { mtime = uint64(time.Now().Unix()) }
	
	tmsp := fuse.NewTimespec(time.Unix(int64(mtime), 0))
	f.statCache.Store(realPath, CachedStat{ Size: size, IsDir: isDir, Exists: true, Timestamp: time.Now(), SysTime: tmsp })
	
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
	fill(".", nil, 0); fill("..", nil, 0)
	payload, err := f.callVfs(OP_LIST, realPath, 0, 0, nil)
	if err != nil || len(payload) == 0 { return 0 }
	uid, gid, _ := fuse.Getcontext()
	tokens := strings.Split(string(payload), "|")
	for _, token := range tokens {
		if token == "" { continue }
		parts := strings.Split(token, ",")
		if len(parts) < 2 { continue }
		name := parts[0]; isDir := parts[1] == "DIR"
		if isADS(name) || isWindowsSystemProbe(name) { continue }
		var st fuse.Stat_t
		st.Uid, st.Gid = uid, gid
		if isDir { st.Mode = fuse.S_IFDIR | 0777 } else { st.Mode = fuse.S_IFREG | 0777 }
		fill(name, &st, 0)
	}
	return 0
}
func (f *ZhiAuthFuse) Read(path string, buff []byte, ofst int64, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	totalReq := len(buff)
	if totalReq == 0 { return 0 }
    
    // 🔥 LỌC BỚT NHỮNG LỆNH ĐỌC LÉN RÁC CỦA EXPLORER
    if strings.HasSuffix(strings.ToLower(realPath), ".zip") && uint32(totalReq) < 65536 {
        return 0 // Chặn đọc Header ZIP vớ vẩn để tránh nghẽn luồng
    }
    
	if f.protocol == "QUIC" {
		chunk, err := f.callVfs(OP_READ, realPath, uint64(ofst), uint32(totalReq), nil)
		if err != nil || len(chunk) == 0 { return 0 }
		return copy(buff, chunk)
	}
	const maxChunk = 131072 
	totalRead := 0
	for totalRead < totalReq {
		chunkSize := totalReq - totalRead
		if chunkSize > maxChunk { chunkSize = maxChunk }
		currentOffset := uint64(ofst) + uint64(totalRead)
		chunk, err := f.callVfs(OP_READ, realPath, currentOffset, uint32(chunkSize), nil)
		if err != nil || len(chunk) == 0 { break }
		n := copy(buff[totalRead:], chunk)
		totalRead += n
		if len(chunk) < chunkSize { break }
	}
	return totalRead
}
func (f *ZhiAuthFuse) Write(path string, buff []byte, ofst int64, fh uint64) int {
	if isADS(path) { return len(buff) } 
	realPath := f.getVfsPath(path)
	totalLen := len(buff)
	ts := time.Now().Format("15:04:05.000")
	if totalLen == 0 {
		f.clearStatCache(realPath)
		f.callVfs(OP_WRITE, realPath, uint64(ofst), 0, []byte{})
		return 0
	}
	if f.protocol == "QUIC" {
		f.clearStatCache(realPath)
		_, err := f.callVfs(OP_WRITE, realPath, uint64(ofst), uint32(totalLen), buff)
		if err != nil { log.Printf("[%s] ❌ [VFS-WRITE-ERR] %v", ts, err); return -fuse.EIO }
		return totalLen
	}
	const maxWriteChunk = 131072
	written := 0
	for written < totalLen {
		chunkSize := totalLen - written
		if chunkSize > maxWriteChunk { chunkSize = maxWriteChunk }
		currentOffset := uint64(ofst) + uint64(written)
		f.clearStatCache(realPath)
		_, err := f.callVfs(OP_WRITE, realPath, currentOffset, uint32(chunkSize), buff[written:written+chunkSize])
		if err != nil {
			if written > 0 { return written }
			log.Printf("[%s] ❌ [VFS-WRITE-ERR] %v", ts, err)
			return -fuse.EIO
		}
		written += chunkSize
	}
	return written
}
func (f *ZhiAuthFuse) Truncate(path string, size int64, fh uint64) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	_, err := f.callVfs(OP_TRUNCATE, realPath, uint64(size), 0, nil)
	if err != nil { return -fuse.EIO }
	return 0
}
func (f *ZhiAuthFuse) Unlink(path string) int {
	if isADS(path) { return 0 }
	realPath := f.getVfsPath(path)
	f.clearStatCache(realPath)
	ts := time.Now().Format("15:04:05.000")
	log.Printf("[%s] [WINFSP-%s] 🗑️ UNLINK FILE: %s", ts, f.protocol, realPath)
	_, err := f.callVfs(OP_DELETE, realPath, 0, 0, nil)
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
func (f *ZhiAuthFuse) Rename(oldpath string, newpath string) int {
	if isADS(oldpath) || isADS(newpath) { return 0 }
	realOldPath := f.getVfsPath(oldpath)
	realNewPath := f.getVfsPath(newpath)
	f.clearStatCache(realOldPath); f.clearStatCache(realNewPath)
	ts := time.Now().Format("15:04:05.000")
	log.Printf("[%s] [WINFSP-%s] 🚚 RENAME: %s -> %s", ts, f.protocol, realOldPath, realNewPath)
	_, err := f.callVfs(OP_RENAME, realOldPath, 0, 0, []byte(realNewPath))
	if err != nil { return -fuse.EIO }
	return 0
}
func (f *ZhiAuthFuse) Utimens(path string, tmsp []fuse.Timespec) int { return 0 }
func (f *ZhiAuthFuse) Chmod(path string, mode uint32) int { return 0 }
func (f *ZhiAuthFuse) Chown(path string, uid uint32, gid uint32) int { return 0 }
func (f *ZhiAuthFuse) Flush(path string, fh uint64) int { return 0 }
func (f *ZhiAuthFuse) Release(path string, fh uint64) int { return 0 }
func (f *ZhiAuthFuse) Releasedir(path string, fh uint64) int { return 0 }

func MountWinFspDrive(driveLetter string, remoteBase string, tunnel *QuicTunnel, protocol string) {
	fsImpl := &ZhiAuthFuse{ remoteBase: remoteBase, protocol: protocol, tunnel: tunnel }
	host := fuse.NewFileSystemHost(fsImpl)
	host.SetCapReaddirPlus(false)
	mountPoint := driveLetter + ":"
    // 🔥 BƠM LẠI 16 LUỒNG ĐỂ TRỊ KẸT XE (Multi-Threading vừa đủ)
	options := []string{"-f", "-o", "file_system_name=ZhiAuth-" + protocol, "-o", "volname=ZhiAuth " + protocol, "-o", "allow_other", "-o", "ThreadCount=16", "-o", "FileInfoTimeout=1000", "-o", "DirInfoTimeout=1000"}
	go func() { host.Mount(mountPoint, options) }()
}
