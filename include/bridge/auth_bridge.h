#ifndef AUTH_BRIDGE_H
#define AUTH_BRIDGE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int zhiauth_core_init(const char* db_path, const char* master_key, int auth_port, int kcp_port, int quic_port, const char* cert_path, const char* key_path, int nodelay, int interval, int resend, int nc, int snd_wnd, int rcv_wnd);
const char* zhiauth_authenticate_and_trigger(const char* username, const char* password, const char* client_lan, const char* client_ts, const char* client_hwid);
void zhiauth_revoke_access(const char* client_ip, const char* shared_path);
uint64_t zhiauth_check_kcp_active(const char* client_ip);
void zhiauth_core_shutdown();

#ifdef __cplusplus
}
#endif
#endif
