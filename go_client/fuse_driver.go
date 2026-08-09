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
	oneSec := 1 * time.Second
	opts := &fs.Options{
		EntryTimeout: &oneSec,
		AttrTimeout:  &oneSec,
		MountOptions: fuse.MountOptions{
			AllowOther:    true,
			Options:       []string{"allow_other"},
			MaxWrite:      32768,
			MaxBackground: 4,
			Debug:         true, // 🔥 BẬT DÒNG NÀY LÊN: Mắt thần soi mọi System Call
		},
	}

	kcpPath := filepath.Join(mountPoint, "VFS_DRIVE")
	quicPath := filepath.Join(mountPoint, "QUIC_DRIVE")
	os.MkdirAll(kcpPath, 0755)
	os.MkdirAll(quicPath, 0755)

	kcpServer, _ := fs.Mount(kcpPath, &KcpVfsNode{remotePath: remoteBase}, opts)
	quicServer, _ := fs.Mount(quicPath, &QuicVfsNode{remotePath: remoteBase, tunnel: tunnel}, opts)

	if kcpServer != nil {
		go kcpServer.Wait()
	}
	if quicServer != nil {
		go quicServer.Wait()
	}
}

func (n *KcpVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	out.SetTimeout(1 * time.Second) // 🔥 CACHE 1 GIÂY

	if n.remotePath == "/" || n.remotePath == "" || strings.HasSuffix(n.remotePath, "HDD_merge") || strings.HasSuffix(n.remotePath, "Huang_Datas") {
		out.Attr.Mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
		return 0
	}

	payload, err := SendRpcVfs(OP_STAT, n.remotePath, 0, 0, nil)
	if err != nil || len(payload) < 37 { // 🔥 KIỂM TRA ĐỦ 37 BYTES
		return syscall.ENOENT
	}

	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	mtime := binary.LittleEndian.Uint64(payload[9:17])
	ctime := binary.LittleEndian.Uint64(payload[17:25])
	atime := binary.LittleEndian.Uint64(payload[25:33])
	modeRaw := binary.LittleEndian.Uint32(payload[33:37])

	if isDir {
		out.Attr.Mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
	} else {
		out.Attr.Mode = modeRaw // 🔥 LẤY CHUẨN MODE GỐC TỪ SERVER
		out.Attr.Nlink = 1
	}
	out.Attr.Size = size
	out.Attr.Mtime = mtime
	out.Attr.Ctime = ctime
	out.Attr.Atime = atime
	return 0
}

func (n *KcpVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)

	payload, err := SendRpcVfs(OP_STAT, fullPath, 0, 0, nil)
	if err != nil || len(payload) < 37 {
		// 🔥 CHỈ SỬA Ở ĐÂY: Trả về 0 để CẤM KERNEL CACHE LỖI "KHÔNG TỒN TẠI"
		out.SetEntryTimeout(0)
		return nil, syscall.ENOENT
	}

	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	mtime := binary.LittleEndian.Uint64(payload[9:17])
	ctime := binary.LittleEndian.Uint64(payload[17:25])
	atime := binary.LittleEndian.Uint64(payload[25:33])
	modeRaw := binary.LittleEndian.Uint32(payload[33:37])

	mode := modeRaw
	out.Attr.Nlink = 1
	if isDir {
		mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
	}

	out.Attr.Size = size
	out.Attr.Mode = mode
	out.Attr.Mtime = mtime
	out.Attr.Ctime = ctime
	out.Attr.Atime = atime
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	out.SetEntryTimeout(1 * time.Second) // 🔥 CACHE 1 GIÂY

	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
}

func (n *KcpVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	payload, err := SendRpcVfs(OP_LIST, n.remotePath, 0, 0, nil)
	if err != nil {
		return nil, syscall.EIO
	}

	InjectBulkCache(n.remotePath, payload) // Truyền thẳng []byte

	var entries []fuse.DirEntry
	offset := 0
	totalLen := len(payload)

	for offset+15 <= totalLen {
		nameLen := int(binary.LittleEndian.Uint16(payload[offset : offset+2]))
		isDirVal := payload[offset+2]

		offset += 15

		if offset+nameLen > totalLen {
			break
		}

		name := string(payload[offset : offset+nameLen])
		offset += nameLen

		mode := uint32(syscall.S_IFREG)
		if isDirVal == 1 {
			mode = syscall.S_IFDIR
		}
		entries = append(entries, fuse.DirEntry{Name: name, Mode: mode})
	}
	return fs.NewListDirStream(entries), 0
}

func (n *KcpVfsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	return nil, 0, 0
}

func (n *KcpVfsNode) Read(ctx context.Context, fh fs.FileHandle, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	payload, err := SendRpcVfs(OP_READ, n.remotePath, uint64(off), uint32(len(dest)), nil)
	if err != nil {
		return nil, syscall.EIO
	}
	copy(dest, payload)
	return fuse.ReadResultData(payload), 0
}

func (n *KcpVfsNode) Write(ctx context.Context, fh fs.FileHandle, buf []byte, off int64) (uint32, syscall.Errno) {
	_, err := SendRpcVfs(OP_WRITE, n.remotePath, uint64(off), 0, buf)
	if err != nil {
		return 0, syscall.EIO
	}
	RemoveFromRAMCache(n.remotePath) // 🔥 Báo cho RAM biết file đã bị sửa dung lượng
	return uint32(len(buf)), 0
}

func (n *KcpVfsNode) Fsync(ctx context.Context, fh fs.FileHandle, flags uint32) syscall.Errno {
	return 0
}

func (n *KcpVfsNode) Mkdir(ctx context.Context, name string, mode uint32, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	_, err := SendRpcVfs(OP_MKDIR, fullPath, 0, 0, nil)
	if err != nil {
		return nil, syscall.EIO
	}
	out.Attr.Mode = syscall.S_IFDIR | 0777
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: syscall.S_IFDIR}), 0
}

func (n *KcpVfsNode) Unlink(ctx context.Context, name string) syscall.Errno {
	fullPath := filepath.Join(n.remotePath, name)
	RemoveFromRAMCache(fullPath)
	_, err := SendRpcVfs(OP_DELETE, fullPath, 0, 0, nil)
	if err != nil {
		return syscall.EIO
	}
	return 0
}

func (n *KcpVfsNode) Rmdir(ctx context.Context, name string) syscall.Errno {
	return n.Unlink(ctx, name)
}

func (n *KcpVfsNode) Rename(ctx context.Context, oldName string, newParent fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldPath := filepath.Join(n.remotePath, oldName)
	newParentNode := newParent.(*KcpVfsNode)
	newPath := filepath.Join(newParentNode.remotePath, newName)

	log.Printf("[FUSE-RENAME] LibreOffice đang Rename: '%s' -> '%s' (Flags: %d)", oldPath, newPath, flags)

	RemoveFromRAMCache(oldPath)
	RemoveFromRAMCache(newPath)

	_, err := SendRpcVfs(OP_RENAME, oldPath, 0, 0, []byte(newPath))
	if err != nil {
		log.Printf("[FUSE-RENAME] ❌ RENAME TỪ SERVER BÁO LỖI: %v", err)
		return syscall.EIO
	}
	log.Printf("[FUSE-RENAME] ✅ Xong!")
	return 0
}

func (n *KcpVfsNode) Create(ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	_, err := SendRpcVfs(OP_WRITE, fullPath, 0, 0, []byte{})
	if err != nil {
		return nil, nil, 0, syscall.EIO
	}
	out.Attr.Mode = syscall.S_IFREG | 0777
	out.Attr.Nlink = 1
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &KcpVfsNode{remotePath: fullPath}, fs.StableAttr{Mode: syscall.S_IFREG}), nil, 0, 0
}

func (n *KcpVfsNode) Setattr(ctx context.Context, fh fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	log.Printf("[FUSE-SETATTR] LibreOffice muốn đổi thuộc tính file: %s", n.remotePath)
	if size, ok := in.GetSize(); ok {
		log.Printf("[FUSE-SETATTR] -> Cắt file (Truncate) về size: %d", size)
		_, err := SendRpcVfs(OP_TRUNCATE, n.remotePath, size, 0, nil)
		if err != nil {
			log.Printf("[FUSE-SETATTR] ❌ TRUNCATE LỖI: %v", err)
			return syscall.EIO
		}
	}
	RemoveFromRAMCache(n.remotePath)
	return n.Getattr(ctx, fh, out)
}

// QUIC VFS Node
// --- HÀM GETATTR CHO QUIC ---
func (n *QuicVfsNode) Getattr(ctx context.Context, fh fs.FileHandle, out *fuse.AttrOut) syscall.Errno {
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	out.SetTimeout(1 * time.Second) // 🔥 CACHE 1 GIÂY

	if n.remotePath == "/" || n.remotePath == "" || strings.HasSuffix(n.remotePath, "HDD_merge") || strings.HasSuffix(n.remotePath, "Huang_Datas") {
		out.Attr.Mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
		return 0
	}

	req, _ := BuildVfsPacket(OP_STAT, n.remotePath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil || len(payload) < 37 { // 🔥 KIỂM TRA ĐỦ 37 BYTES
		return syscall.ENOENT
	}

	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	mtime := binary.LittleEndian.Uint64(payload[9:17])
	ctime := binary.LittleEndian.Uint64(payload[17:25])
	atime := binary.LittleEndian.Uint64(payload[25:33])
	modeRaw := binary.LittleEndian.Uint32(payload[33:37])

	if isDir {
		out.Attr.Mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
	} else {
		out.Attr.Mode = modeRaw // 🔥 LẤY CHUẨN MODE GỐC TỪ SERVER
		out.Attr.Nlink = 1
	}
	out.Attr.Size = size
	out.Attr.Mtime = mtime
	out.Attr.Ctime = ctime
	out.Attr.Atime = atime
	return 0
}

// --- HÀM LOOKUP CHO QUIC ---
func (n *QuicVfsNode) Lookup(ctx context.Context, name string, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)

	req, _ := BuildVfsPacket(OP_STAT, fullPath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil || len(payload) < 37 { // 🔥 KIỂM TRA ĐỦ 37 BYTES
		out.SetEntryTimeout(0)
		return nil, syscall.ENOENT
	}

	size := binary.LittleEndian.Uint64(payload[0:8])
	isDir := payload[8] == 1
	mtime := binary.LittleEndian.Uint64(payload[9:17])
	ctime := binary.LittleEndian.Uint64(payload[17:25])
	atime := binary.LittleEndian.Uint64(payload[25:33])
	modeRaw := binary.LittleEndian.Uint32(payload[33:37])

	mode := modeRaw
	out.Attr.Nlink = 1
	if isDir {
		mode = syscall.S_IFDIR | 0777
		out.Attr.Nlink = 2
	}

	out.Attr.Size = size
	out.Attr.Mode = mode
	out.Attr.Mtime = mtime
	out.Attr.Ctime = ctime
	out.Attr.Atime = atime
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	out.SetEntryTimeout(1 * time.Second) // 🔥 CACHE 1 GIÂY

	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: mode & syscall.S_IFMT}), 0
}

func (n *QuicVfsNode) Readdir(ctx context.Context) (fs.DirStream, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_LIST, n.remotePath, 0, 0, nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return nil, syscall.EIO
	}

	InjectBulkCache(n.remotePath, payload) // Truyền thẳng []byte

	var entries []fuse.DirEntry
	offset := 0
	totalLen := len(payload)

	for offset+15 <= totalLen {
		nameLen := int(binary.LittleEndian.Uint16(payload[offset : offset+2]))
		isDirVal := payload[offset+2]

		offset += 15

		if offset+nameLen > totalLen {
			break
		}

		name := string(payload[offset : offset+nameLen])
		offset += nameLen

		mode := uint32(syscall.S_IFREG)
		if isDirVal == 1 {
			mode = syscall.S_IFDIR
		}
		entries = append(entries, fuse.DirEntry{Name: name, Mode: mode})
	}
	return fs.NewListDirStream(entries), 0
}

func (n *QuicVfsNode) Open(ctx context.Context, flags uint32) (fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	return nil, 0, 0
}

func (n *QuicVfsNode) Read(ctx context.Context, fh fs.FileHandle, dest []byte, off int64) (fuse.ReadResult, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_READ, n.remotePath, uint64(off), uint32(len(dest)), nil)
	payload, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return nil, syscall.EIO
	}
	copy(dest, payload)
	return fuse.ReadResultData(payload), 0
}

func (n *QuicVfsNode) Write(ctx context.Context, fh fs.FileHandle, buf []byte, off int64) (uint32, syscall.Errno) {
	req, _ := BuildVfsPacket(OP_WRITE, n.remotePath, uint64(off), 0, buf)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return 0, syscall.EIO
	}
	return uint32(len(buf)), 0
}

func (n *QuicVfsNode) Fsync(ctx context.Context, fh fs.FileHandle, flags uint32) syscall.Errno {
	return 0
}

func (n *QuicVfsNode) Mkdir(ctx context.Context, name string, mode uint32, out *fuse.EntryOut) (*fs.Inode, syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	req, _ := BuildVfsPacket(OP_MKDIR, fullPath, 0, 0, nil)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return nil, syscall.EIO
	}
	out.Attr.Mode = syscall.S_IFDIR | 0777
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: syscall.S_IFDIR}), 0
}

func (n *QuicVfsNode) Unlink(ctx context.Context, name string) syscall.Errno {
	fullPath := filepath.Join(n.remotePath, name)
	RemoveFromRAMCache(fullPath)
	req, _ := BuildVfsPacket(OP_DELETE, fullPath, 0, 0, nil)
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return syscall.EIO
	}
	return 0
}

func (n *QuicVfsNode) Rmdir(ctx context.Context, name string) syscall.Errno {
	return n.Unlink(ctx, name)
}

func (n *QuicVfsNode) Rename(ctx context.Context, oldName string, newParent fs.InodeEmbedder, newName string, flags uint32) syscall.Errno {
	oldPath := filepath.Join(n.remotePath, oldName)
	newParentNode := newParent.(*QuicVfsNode)
	newPath := filepath.Join(newParentNode.remotePath, newName)
	RemoveFromRAMCache(oldPath)
	req, _ := BuildVfsPacket(OP_RENAME, oldPath, 0, 0, []byte(newPath))
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return syscall.EIO
	}
	return 0
}

func (n *QuicVfsNode) Create(ctx context.Context, name string, flags uint32, mode uint32, out *fuse.EntryOut) (node *fs.Inode, fh fs.FileHandle, fuseFlags uint32, errno syscall.Errno) {
	fullPath := filepath.Join(n.remotePath, name)
	req, _ := BuildVfsPacket(OP_WRITE, fullPath, 0, 0, []byte{})
	_, err := n.tunnel.SendFsCommandRaw(req)
	if err != nil {
		return nil, nil, 0, syscall.EIO
	}
	out.Attr.Mode = syscall.S_IFREG | 0777
	out.Attr.Nlink = 1
	out.Attr.Uid, out.Attr.Gid = uint32(os.Getuid()), uint32(os.Getgid())
	return n.NewInode(ctx, &QuicVfsNode{remotePath: fullPath, tunnel: n.tunnel}, fs.StableAttr{Mode: syscall.S_IFREG}), nil, 0, 0
}

func (n *QuicVfsNode) Setattr(ctx context.Context, fh fs.FileHandle, in *fuse.SetAttrIn, out *fuse.AttrOut) syscall.Errno {
	if size, ok := in.GetSize(); ok {
		req, _ := BuildVfsPacket(OP_TRUNCATE, n.remotePath, size, 0, nil)
		_, err := n.tunnel.SendFsCommandRaw(req)
		if err != nil {
			return syscall.EIO
		}
	}
	return n.Getattr(ctx, fh, out)
}
