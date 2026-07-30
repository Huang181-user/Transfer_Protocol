package main

import (
	"context"
	"encoding/binary"
	"log"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/hanwen/go-fuse/v2/fs"
	"github.com/hanwen/go-fuse/v2/fuse"
)

type KcpVfsNode struct {
	fs.Inode
	remotePath string
}

type QuicVfsNode struct {
	fs.Inode
	remotePath string
	tunnel     *QuicTunnel
}

func StartDualFuseSubsystem(mountPoint, remoteBase string, tunnel *QuicTunnel) {
	oneHour := 24 * time.Hour
	opts := &fs.Options{
		EntryTimeout: &oneHour,
		AttrTimeout:  &oneHour,
		MountOptions: fuse.MountOptions{
			AllowOther: true,
			MaxWrite:   131072,
			Debug:      false, // 🔥 BẬT MẮT THẦN SOI THUNAR
		},
	}

	kcpPath := filepath.Join(mountPoint, "VFS_DRIVE")
	quicPath := filepath.Join(mountPoint, "QUIC_DRIVE")
	os.MkdirAll(kcpPath, 0755)
	os.MkdirAll(quicPath, 0755)

	log.Printf("[%s] [FUSE-INIT] Mount KCP C++: %s", GetTimestamp(), kcpPath)
	kcpServer, errKcp := fs.Mount(kcpPath, &KcpVfsNode{remotePath: remoteBase}, opts)
	if errKcp != nil { log.Printf("[%s] [ERROR] Lỗi Mount KCP: %v", GetTimestamp(), errKcp) }

	log.Printf("[%s] [FUSE-INIT] Mount QUIC: %s", GetTimestamp(), quicPath)
	quicServer, errQuic := fs.Mount(quicPath, &QuicVfsNode{remotePath: remoteBase, tunnel: tunnel}, opts)
	if errQuic != nil { log.Printf("[%s] [ERROR] Lỗi Mount QUIC: %v", GetTimestamp(), errQuic) }

	if kcpServer != nil { go kcpServer.Wait() }
	if quicServer != nil { go quicServer.Wait() }
}

func (n *KcpVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	log.Printf("[FUSE-KCP] 👉 Gọi Getattr: %s", n.remotePath)
	uid, gid := uint32(os.Getuid()), uint32(os.Getgid())
	out.Attr.Uid, out.Attr.Gid = uid, gid
	out.Attr.Mode = syscall.S_IFREG | 0777
	out.SetTimeout(60 * time.Second)

	if n.remotePath == "/" || n.remotePath == "" || strings.HasSuffix(n.remotePath, "HDD_merge") || strings.HasSuffix(n.remotePath, "Huang_Datas") {
		out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2
		log.Printf("[FUSE-KCP] ✅ Getattr Root OK: %s", n.remotePath)
		return 0
	}

	if meta, exists := CheckRAMCache(n.remotePath); exists {
		if meta.IsDir { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
		out.Attr.Size = meta.Size
		log.Printf("[FUSE-KCP] ✅ Getattr từ CACHE OK: %s", n.remotePath)
		return 0
	}

	payload, err := SendRpcVfs(OP_STAT, n.remotePath, 0, 0, nil)
	if err != nil || len(payload) < 9 { 
		log.Printf("[FUSE-KCP] ❌ Getattr LỖI KCP: %s", n.remotePath)
		return syscall.ENOENT 
	}
	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	if isDir { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
	out.Attr.Size = size
	log.Printf("[FUSE-KCP] ✅ Getattr từ SERVER OK: %s", n.remotePath)
	return 0
}

func (n *KcpVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	log.Printf("[FUSE-KCP] 🔍 Gọi Lookup: %s", fullPath)

	if meta, exists := CheckRAMCache(fullPath); exists {
		mode := uint32(syscall.S_IFREG | 0777)
		out.Attr.Nlink = 1
		if meta.IsDir { mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 }
		out.Attr.Size = meta.Size
		out.Attr.Mode = mode
		out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
		out.SetEntryTimeout(60 * time.Second)
		log.Printf("[FUSE-KCP] ✅ Lookup từ CACHE OK: %s", fullPath)
		return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
	}

	payload, err := SendRpcVfs(OP_STAT, fullPath, 0, 0, nil)
	if err != nil || len(payload) < 9 {
		out.SetEntryTimeout(1 * time.Second)
		log.Printf("[FUSE-KCP] ❌ Lookup LỖI KCP: %s", fullPath)
		return nil, syscall.ENOENT 
	}

	size := binary.LittleEndian.Uint64(payload[0:8])
	mode := uint32(syscall.S_IFREG | 0777)
	out.Attr.Nlink = 1
	if payload[8] == 1 { mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 }
	out.Attr.Size = size
	out.Attr.Mode = mode
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	out.SetEntryTimeout(60 * time.Second)
	log.Printf("[FUSE-KCP] ✅ Lookup từ SERVER OK: %s", fullPath)

	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
}

func (n *KcpVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	log.Printf("[FUSE-KCP] 📂 Gọi Readdir: %s", n.remotePath)
	payload, err := SendRpcVfs(OP_LIST, n.remotePath, 0, 0, nil)
	if err != nil { 
		log.Printf("[FUSE-KCP] ❌ Readdir LỖI KCP: %s", n.remotePath)
		return nil, syscall.EIO 
	}
	
	InjectBulkCache(n.remotePath, string(payload))

	var entries []fuse.DirEntry
	for _, token := range strings.Split(string(payload), "|") {
		if token == "" { continue }
		parts := strings.Split(token, ",")
		if len(parts) < 2 { continue }
		mode := uint32(syscall.S_IFREG)
		if parts[1] == "DIR" { mode = syscall.S_IFDIR }
		entries = append(entries, fuse.DirEntry{Name: parts[0], Mode: mode})
	}
	log.Printf("[FUSE-KCP] ✅ Readdir OK: Đã nạp %d file vào RAM", len(entries))
	return fs.NewListDirStream(entries), 0
}

func (n *KcpVfsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) { 
	log.Printf("[FUSE-KCP] 📖 Gọi Open: %s", n.remotePath)
	return nil, 0, 0 
}

func (n *KcpVfsNode) Read(ctx context.Context, fh fs.FileHandle, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	log.Printf("[FUSE-KCP] 📥 Gọi Read: %s (Offset: %d, Len: %d)", n.remotePath, off, len(dest))
	payload, err := SendRpcVfs(OP_READ, n.remotePath, uint64(off), uint32(len(dest)), nil)
	if err != nil { 
		log.Printf("[FUSE-KCP] ❌ Read LỖI KCP: %s", n.remotePath)
		return nil, syscall.EIO 
	}
	copy(dest, payload)
	log.Printf("[FUSE-KCP] ✅ Read OK: Đã đọc %d bytes", len(payload))
	return fuse.ReadResultData(payload), 0
}

func (n *KcpVfsNode) Write(ctx context.Context, fh fs.FileHandle, buf []byte, off int64) (uint32, syscall.Errno) {
	_, err := SendRpcVfs(OP_WRITE, n.remotePath, uint64(off), 0, buf)
	if err != nil { return 0, syscall.EIO }
	return uint32(len(buf)), 0
}

func (n *KcpVfsNode) Fsync(ctx context.Context, fh fs.FileHandle, flags uint32) syscall.Errno { return 0 }

func (n *KcpVfsNode) Mkdir(ctx context.Context, name string, mode uint32, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	_, err := SendRpcVfs(OP_MKDIR, fullPath, 0, 0, nil)
	if err != nil { return nil, syscall.EIO }
	out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: syscall.S_IFDIR}), 0
}

func (n *KcpVfsNode) Unlink(ctx context.Context, name string) syscall.Errno {
	fullPath := filepath.Join(n.remotePath, name)
	RemoveFromRAMCache(fullPath)
	_, err := SendRpcVfs(OP_DELETE, fullPath, 0, 0, nil)
	if err != nil { return syscall.EIO }
	return 0
}

func (n *KcpVfsNode) Rmdir(ctx context.Context, name string) syscall.Errno { return n.Unlink(ctx, name) }

func (n *KcpVfsNode) Rename(ctx context.Context, oldName string, newParent fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldPath := filepath.Join(n.remotePath, oldName)
	newParentNode := newParent.(*KcpVfsNode)
	newPath := filepath.Join(newParentNode.remotePath, newName)
	RemoveFromRAMCache(oldPath)
	_, err := SendRpcVfs(OP_RENAME, oldPath, 0, 0, []byte(newPath))
	if err != nil { return syscall.EIO }
	return 0
}

func (n *KcpVfsNode) Create(ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	_, err := SendRpcVfs(OP_WRITE, fullPath, 0, 0, []byte{})
	if err != nil { return nil, nil, 0, syscall.EIO }
	out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1; out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: syscall.S_IFREG}), nil, 0, 0
}

func (n *KcpVfsNode) Setattr(ctx context.Context, fh fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	if size, ok := in.GetSize(); ok {
		_, err := SendRpcVfs(OP_TRUNCATE, n.remotePath, size, 0, nil)
		if err != nil { return syscall.EIO }
	}
	return n.Getattr(ctx, fh, out)
}

// Bỏ qua QUIC vì ný đang test trên nội bộ bằng KCP
func (n *QuicVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno { return syscall.ENOENT }
func (n *QuicVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) { return nil, syscall.ENOENT }
func (n *QuicVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) { return nil, syscall.EIO }
