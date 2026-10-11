#ifndef ZhiAuth_Bridging_Header_h
#define ZhiAuth_Bridging_Header_h

#include <stdint.h>
#include <stdbool.h>

// Bắt buộc phải khai báo lại đống hàm này để Swift nhìn thấy C++
void* zhiauth_create_vfs_client(const char* ip, int port, const char* sym_key, int mtu,
                                int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd);
bool zhiauth_start_vfs_client(void* client);
void zhiauth_stop_vfs_client(void* client);
void zhiauth_destroy_vfs_client(void* client);
void zhiauth_vfs_send_rpc(void* client, uint32_t req_id, uint8_t opcode, const char* path, 
                          uint64_t offset, uint32_t req_len, const uint8_t* payload, uint32_t payload_len);

#endif /* ZhiAuth_Bridging_Header_h */
