#ifndef CLIENT_BRIDGE_H
#define CLIENT_BRIDGE_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int zhiauth_client_init(const char* ip, int port, const char* sym_key, int mtu, uint32_t conv,
                      int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd);
void zhiauth_client_shutdown();

// 🔥 ĐỔI TÊN HÀM VÀ BỎ CON TRỎ TRẢ VỀ
void zhiauth_send_vfs_command_async(const uint8_t* payload, size_t len);

// 🔥 KHAI BÁO HÀM CALLBACK VỀ GOLANG
extern void zhiauth_cgo_on_response(uint64_t reqId, uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif
#endif