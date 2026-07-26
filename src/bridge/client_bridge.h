#ifndef CLIENT_BRIDGE_H
#define CLIENT_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int zhiauth_client_init(const char* server_ip, int port, const char* sym_key, int mtu);
uint8_t* zhiauth_send_vfs_command(const uint8_t* payload, size_t payload_len, size_t* out_len);
void zhiauth_client_shutdown();
#ifdef __cplusplus
}
#endif
#endif
