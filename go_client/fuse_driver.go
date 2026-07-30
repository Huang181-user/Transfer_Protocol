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
			AllowOther:    true,
			MaxWrite:      131072,
			MaxBackground: 128,
			// 🔥 BƠM STEROID TĂNG TỐC VÀ ĐỌC TRƯỚC 4MB
			Options: []string{"max_read=131072", "max_readahead=4194304", "kernel_cache", "async_read"},
		},
	}

	kcpPath := filepath.Join(mountPoint, "VFS_DRIVE")
	quicPath := filepath.Join(mountPoint, "QUIC_DRIVE")
	os.MkdirAll(kcpPath, 0755)
	os.MkdirAll(quicPath, 0755)

	kcpServer, _ := fs.Mount(kcpPath, &KcpVfsNode{remotePath: remoteBase}, opts)
	quicServer, _ := fs.Mount(quicPath, &QuicVfsNode{remotePath: remoteBase, tunnel: tunnel}, opts)

	if kcpServer != nil { go kcpServer.Wait() }
	if quicServer != nil { go quicServer.Wait() }
}

func (n *KcpVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	uid, gid := uint32(os.Getuid()), uint32(os.Getgid())
	out.Attr.Uid, out.Attr.Gid = uid, gid
	out.Attr.Mode = syscall.S_IFREG | 0777
	out.SetTimeout(60 * time.Second)

	if n.remotePath == "/" || n.remotePath == "" || strings.HasSuffix(n.remotePath, "HDD_merge") || strings.HasSuffix(n.remotePath, "Huang_Datas") {
		out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2
		return 0
	}

	if meta, exists := CheckRAMCache(n.remotePath); exists {
		if meta.IsDir { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
		out.Attr.Size = meta.Size
		return 0
	}

	payload, err := SendRpcVfs(OP_STAT, n.remotePath, 0, 0, nil)
	if err != nil || len(payload) < 9 { return syscall.ENOENT }
	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	if isDir { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
	out.Attr.Size = size
	return 0
}

func (n *KcpVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	if meta, exists := CheckRAMCache(fullPath); exists {
		mode := uint32(syscall.S_IFREG | 0777)
		out.Attr.Nlink = 1
		if meta.IsDir { mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 }
		out.Attr.Size = meta.Size
		out.Attr.Mode = mode
		out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
		out.SetEntryTimeout(60 * time.Second)
		return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
	}

	payload, err := SendRpcVfs(OP_STAT, fullPath, 0, 0, nil)
	if err != nil || len(payload) < 9 {
		out.SetEntryTimeout(1 * time.Second)
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

	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
}

func (n *KcpVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	payload, err := SendRpcVfs(OP_LIST, n.remotePath, 0, 0, nil)
	if err != nil { return nil, syscall.EIO }
	
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
	return fs.NewListDirStream(entries), 0
}

func (n *KcpVfsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) { return nil, 0, 0 }

func (n *KcpVfsNode) Read(ctx context.Context, fh fs.FileHandle, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	payload, err := SendRpcVfs(OP_READ, n.remotePath, uint64(off), uint32(len(dest)), nil)
	if err != nil { return nil, syscall.EIO }
	copy(dest, payload)
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

// QUIC VFS Node
func (n *QuicVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	uid, gid := uint32(os.Getuid()), uint32(os.Getgid())
	out.Attr.Uid, out.Attr.Gid = uid, gid
	out.Attr.Mode = syscall.S_IFREG | 0777
	out.SetTimeout(60 * time.Second)

	if n.remotePath == "/" || n.remotePath == "" || strings.HasSuffix(n.remotePath, "HDD_merge") || strings.HasSuffix(n.remotePath, "Huang_Datas") {
		out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2
		return 0
	}

	if meta, exists := CheckRAMCache(n.remotePath); exists {
		if meta.IsDir { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
		out.Attr.Size = meta.Size
		return 0
	}

	req, _ := BuildVfsPacket(OP_STAT, n.remotePath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil || len(payload) < 9 { return syscall.ENOENT }
	
	size := binary.LittleEndian.Uint64(payload[0:8])
	if payload[8] == 1 { out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 } else { out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1 }
	out.Attr.Size = size
	return 0
}

func (n *QuicVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	if meta, exists := CheckRAMCache(fullPath); exists {
		mode := uint32(syscall.S_IFREG | 0777)
		out.Attr.Nlink = 1
		if meta.IsDir { mode = syscall.S_IFDIR | 0777; out.Attr.Nlink = 2 }
		out.Attr.Size = meta.Size
		out.Attr.Mode = mode
		out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
		out.SetEntryTimeout(60 * time.Second)
		return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
	}

	req, _ := BuildVfsPacket(OP_STAT, fullPath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil || len(payload) < 9 {
		out.SetEntryTimeout(1 * time.Second)
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

	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
}

func (n *QuicVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_LIST, n.remotePath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return nil, syscall.EIO }

	InjectBulkCache(n.remotePath, string(payload))

	var dirEntries []fuse.DirEntry
	for _, token := range strings.Split(string(payload), "|") {
		if token == "" { continue }
		parts := strings.Split(token, ",")
		if len(parts) < 2 { continue }
		mode := uint32(syscall.S_IFREG)
		if parts[1] == "DIR" { mode = syscall.S_IFDIR }
		dirEntries = append(dirEntries, fuse.DirEntry{Name: parts[0], Mode: mode})
	}
	return fs.NewListDirStream(dirEntries), 0
}

func (n *QuicVfsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) { return nil, 0, 0 }

func (n *QuicVfsNode) Read(ctx context.Context, fh fs.FileHandle, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_READ, n.remotePath, uint64(off), uint32(len(dest)), nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return nil, syscall.EIO }
	copy(dest, payload)
	return fuse.ReadResultData(payload), 0
}

func (n *QuicVfsNode) Write(ctx context.Context, fh fs.FileHandle, buf []byte, off int64) (uint32, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_WRITE, n.remotePath, uint64(off), 0, buf)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return 0, syscall.EIO }
	return uint32(len(buf)), 0
}

func (n *QuicVfsNode) Fsync(ctx context.Context, fh fs.FileHandle, flags uint32) syscall.Errno { return 0 }

func (n *QuicVfsNode) Mkdir(ctx context.Context, name string, mode uint32, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	req, _ := BuildVfsPacket(OP_MKDIR, fullPath, 0, 0, nil)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return nil, syscall.EIO }
	out.Attr.Mode = syscall.S_IFDIR | 0777; out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: syscall.S_IFDIR}), 0
}

func (n *QuicVfsNode) Unlink(ctx context.Context, name string) syscall.Errno {
	fullPath := filepath.Join(n.remotePath, name)
	RemoveFromRAMCache(fullPath)
	req, _ := BuildVfsPacket(OP_DELETE, fullPath, 0, 0, nil)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return syscall.EIO }
	return 0
}

func (n *QuicVfsNode) Rmdir(ctx context.Context, name string) syscall.Errno { return n.Unlink(ctx, name) }

func (n *QuicVfsNode) Rename(ctx context.Context, oldName string, newParent fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldPath := filepath.Join(n.remotePath, oldName)
	newParentNode := newParent.(*QuicVfsNode)
	newPath := filepath.Join(newParentNode.remotePath, newName)
	RemoveFromRAMCache(oldPath)
	req, _ := BuildVfsPacket(OP_RENAME, oldPath, 0, 0, []byte(newPath))
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return syscall.EIO }
	return 0
}

func (n *QuicVfsNode) Create(ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	req, _ := BuildVfsPacket(OP_WRITE, fullPath, 0, 0, []byte{})
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil { return nil, nil, 0, syscall.EIO }
	out.Attr.Mode = syscall.S_IFREG | 0777; out.Attr.Nlink = 1; out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: syscall.S_IFREG}), nil, 0, 0
}

func (n *QuicVfsNode) Setattr(ctx context.Context, fh fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	if size, ok := in.GetSize(); ok {
		req, _ := BuildVfsPacket(OP_TRUNCATE, n.remotePath, size, 0, nil)
		_, err := n.tunnel.SendFsCommandRaw(req)
		if err != nil { return syscall.EIO }
	}
	return n.Getattr(ctx, fh, out)
}
