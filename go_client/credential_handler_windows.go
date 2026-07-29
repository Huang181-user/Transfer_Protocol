//go:build windows
package main

/*
#cgo CFLAGS: -I../src/bridge -I../src
#include "win_auth.h"
#include <stdlib.h>
*/
import "C"
import (
	"log"
	"os"
	"unsafe"
)

type UserCredentials struct {
	Username   string
	Password   string
	MountPoint string
}

func GetUserCredentials() UserCredentials {
	var user, pass [256]C.char
	var save C.int
	log.Println("[CRED-UI] Khởi chạy Native Windows Login Box...")
	if C.win_prompt_cred(&user[0], &pass[0], &save) == 0 {
		log.Fatal("❌ [FATAL] Đăng nhập bị hủy!")
		os.Exit(1)
	}
	if save == 1 {
		C.win_save_cred(&user[0], &pass[0])
	}
	return UserCredentials{
		Username:   C.GoString(&user[0]),
		Password:   C.GoString(&pass[0]),
		MountPoint: "A:",
	}
}

func LoadDeviceSession(filepath string) (string, string, string, bool) {
	var user, pass [256]C.char
	// 🔥 LUÔN KÉO TOKEN TỪ WINDOWS VAULT RA ĐỂ TÁI SỬ DỤNG NGẦM
	if C.win_load_cred(&user[0], &pass[0]) == 1 {
		log.Println("[CRED-LOAD] ✅ Mở khóa thành công Token bảo mật từ Windows Vault!")
		return C.GoString(&user[0]), C.GoString(&pass[0]), "A:", true
	}
	return "", "", "", false
}

func SaveDeviceSession(filepath, user, pass, mountPath string) bool {
	cUser := C.CString(user)
	cPass := C.CString(pass)
	// 🔥 VÁ TỬ HUYỆT: Sửa thành unsafe.Pointer (dấu chấm)
	defer C.free(unsafe.Pointer(cUser))
	defer C.free(unsafe.Pointer(cPass))
	
	C.win_save_cred(cUser, cPass)
	log.Println("[CRED-SAVE] 💾 Đã khóa cứng thông tin Session vào Windows Vault thành công.")
	return true
}

func ClearDeviceSession(filepath string) {
	C.win_delete_cred()
	log.Println("[CRED-CLEAR] 🗑️ Đã tiêu hủy thẻ bài Token trong Windows Vault.")
}
