#ifndef CLIENT_BRIDGE_H
#define CLIENT_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int zhiauth_client_init(const char* server_ip, int port, const char* sym_key, int mtu);
// Gọi cái này là trả lời qua hàm callback, siêu mượt!
void zhiauth_send_vfs_command_async(const uint8_t* payload, size_t payload_len);
void zhiauth_client_shutdown();

// Khai báo hàm Go để gọi ngược lên
extern void zhiauth_cgo_on_response(uint64_t reqId, uint8_t* data, size_t length);
#ifdef __cplusplus
}
#endif
#endif
