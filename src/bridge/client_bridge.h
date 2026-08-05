#ifndef CLIENT_BRIDGE_H
#define CLIENT_BRIDGE_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 🔥 Đã bổ sung các tham số KCP Tuning động từ Server
int zhiauth_client_init(const char* ip, int port, const char* sym_key, int mtu, uint32_t conv,
                      int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd);
void zhiauth_client_shutdown();
uint8_t* zhiauth_send_vfs_command(const uint8_t* payload, size_t len, size_t* out_len);

#ifdef __cplusplus
}
#endif
#endif