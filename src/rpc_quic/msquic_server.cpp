#include "rpc_quic/msquic_server.h"
#include "common/logger.h"
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <filesystem>
#include <arpa/inet.h>

extern "C" const char* go_on_msquic_auth(const char* payload, const char* remote_ip);
extern "C" const char* go_get_username_by_ip(const char* ip);

const QUIC_BUFFER ALPN_BUFFER = { sizeof("zhiauth-rpc") - 1, (uint8_t*)"zhiauth-rpc" };
static const QUIC_API_TABLE* MsQuic = nullptr;
static HQUIC Registration = nullptr;
static HQUIC Configuration = nullptr;
static HQUIC AuthListener = nullptr;
static HQUIC DataListener = nullptr;

static std::string g_absolute_cert_path;
static std::string g_absolute_key_path;

struct StreamContext { std::vector<uint8_t> buffer; std::string remote_ip; };

bool MsQuicServer::initialize(uint16_t auth_port, uint16_t data_port, const std::string& cert_path, const std::string& key_path) {
    std::error_code ec;
    g_absolute_cert_path = std::filesystem::absolute(cert_path, ec).string();
    g_absolute_key_path = std::filesystem::absolute(key_path, ec).string();

    if (QUIC_FAILED(MsQuicOpen2(&MsQuic))) return false;
    const QUIC_REGISTRATION_CONFIG RegConfig = { "ZhiAuthServer", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    MsQuic->RegistrationOpen(&RegConfig, &Registration);

    QUIC_SETTINGS Settings;
    memset(&Settings, 0, sizeof(Settings));
    Settings.PeerBidiStreamCount = 1000; Settings.IsSet.PeerBidiStreamCount = 1;
    Settings.IdleTimeoutMs = 120000; Settings.IsSet.IdleTimeoutMs = 1;
    // 🔥 THUỐC CHỐNG NGỦ GẬT NẰM Ở ĐÂY: Ép MsQUIC tự bắn Ping mỗi 15 giây giữ mạng luôn thông!
    Settings.KeepAliveIntervalMs = 15000; Settings.IsSet.KeepAliveIntervalMs = 1; 
    MsQuic->ConfigurationOpen(Registration, &ALPN_BUFFER, 1, &Settings, sizeof(Settings), nullptr, &Configuration);

    QUIC_CERTIFICATE_FILE CertFile;
    memset(&CertFile, 0, sizeof(CertFile));
    CertFile.PrivateKeyFile = g_absolute_key_path.c_str(); 
    CertFile.CertificateFile = g_absolute_cert_path.c_str();

    QUIC_CREDENTIAL_CONFIG CredConfig;
    memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_FILE;
    CredConfig.CertificateFile = &CertFile;
    CredConfig.Flags = QUIC_CREDENTIAL_FLAG_NONE;

    MsQuic->ConfigurationLoadCredential(Configuration, &CredConfig);

    QUIC_ADDR AuthAddr; memset(&AuthAddr, 0, sizeof(AuthAddr));
    AuthAddr.Ipv4.sin_family = AF_INET; AuthAddr.Ipv4.sin_port = htons(auth_port); AuthAddr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    MsQuic->ListenerOpen(Registration, AuthListenerCallback, nullptr, &AuthListener);
    MsQuic->ListenerStart(AuthListener, &ALPN_BUFFER, 1, &AuthAddr);

    QUIC_ADDR DataAddr; memset(&DataAddr, 0, sizeof(DataAddr));
    DataAddr.Ipv4.sin_family = AF_INET; DataAddr.Ipv4.sin_port = htons(data_port); DataAddr.Ipv4.sin_addr.s_addr = INADDR_ANY;
    MsQuic->ListenerOpen(Registration, DataListenerCallback, nullptr, &DataListener);
    MsQuic->ListenerStart(DataListener, &ALPN_BUFFER, 1, &DataAddr);

    return true;
}

void MsQuicServer::shutdown() {
    if (AuthListener) MsQuic->ListenerClose(AuthListener);
    if (DataListener) MsQuic->ListenerClose(DataListener);
    if (Configuration) MsQuic->ConfigurationClose(Configuration);
    if (Registration) MsQuic->RegistrationClose(Registration);
    if (MsQuic) MsQuicClose(MsQuic);
}

QUIC_STATUS QUIC_API MsQuicServer::AuthStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    auto ctx = static_cast<StreamContext*>(Context);
    if (Event->Type == QUIC_STREAM_EVENT_RECEIVE) {
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) ctx->buffer.insert(ctx->buffer.end(), Event->RECEIVE.Buffers[i].Buffer, Event->RECEIVE.Buffers[i].Buffer + Event->RECEIVE.Buffers[i].Length);
        std::string payload_str(ctx->buffer.begin(), ctx->buffer.end());
        const char* go_res = go_on_msquic_auth(payload_str.c_str(), ctx->remote_ip.c_str());
        if (go_res) {
            std::string res_str = go_res; free((void*)go_res);
            QUIC_BUFFER* qb = new QUIC_BUFFER; qb->Buffer = new uint8_t[res_str.length()]; memcpy(qb->Buffer, res_str.data(), res_str.length()); qb->Length = res_str.length();
            MsQuic->StreamSend(Stream, qb, 1, QUIC_SEND_FLAG_FIN, qb);
        }
    } else if (Event->Type == QUIC_STREAM_EVENT_SEND_COMPLETE) {
        QUIC_BUFFER* qb = static_cast<QUIC_BUFFER*>(Event->SEND_COMPLETE.ClientContext);
        if (qb) { delete[] qb->Buffer; delete qb; }
    } else if (Event->Type == QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE) {
        delete ctx; MsQuic->StreamClose(Stream);
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API MsQuicServer::AuthConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event) {
    if (Event->Type == QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED) {
        QUIC_ADDR RemoteAddr;
        uint32_t AddrLen = sizeof(RemoteAddr);
        MsQuic->GetParam(Connection, QUIC_PARAM_CONN_REMOTE_ADDRESS, &AddrLen, &RemoteAddr);
        
        char ip_str[INET6_ADDRSTRLEN] = {0};
        if (RemoteAddr.Ipv4.sin_family == AF_INET) inet_ntop(AF_INET, &(RemoteAddr.Ipv4.sin_addr), ip_str, INET_ADDRSTRLEN);
        else inet_ntop(AF_INET6, &(RemoteAddr.Ipv6.sin6_addr), ip_str, INET6_ADDRSTRLEN);
        
        std::string ip(ip_str);
        if (ip.find("::ffff:") == 0) ip = ip.substr(7); 

        auto ctx = new StreamContext{{}, ip};
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, (void*)AuthStreamCallback, ctx);
    } else if (Event->Type == QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
        MsQuic->ConnectionClose(Connection);
    }
    return QUIC_STATUS_SUCCESS;
}

void MsQuicServer::ProxyUdsTask(HQUIC Stream, std::string uds_path, std::vector<uint8_t> payload) {
    int uds_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr; memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX; strncpy(addr.sun_path, uds_path.c_str(), sizeof(addr.sun_path) - 1);
    struct timeval tv{10, 0};
    setsockopt(uds_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); setsockopt(uds_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(uds_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0); close(uds_fd); return; }
    uint32_t req_sz = payload.size();
    send(uds_fd, &req_sz, 4, MSG_NOSIGNAL); send(uds_fd, payload.data(), payload.size(), MSG_NOSIGNAL);
    uint32_t resp_sz = 0;
    if (recv(uds_fd, &resp_sz, 4, MSG_WAITALL) <= 0) { MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0); close(uds_fd); return; }
    std::vector<uint8_t> resp(resp_sz);
    recv(uds_fd, resp.data(), resp_sz, MSG_WAITALL); close(uds_fd);
    QUIC_BUFFER* qb = new QUIC_BUFFER; qb->Buffer = new uint8_t[resp_sz]; memcpy(qb->Buffer, resp.data(), resp_sz); qb->Length = resp_sz;
    MsQuic->StreamSend(Stream, qb, 1, QUIC_SEND_FLAG_FIN, qb);
}

QUIC_STATUS QUIC_API MsQuicServer::DataStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    auto ctx = static_cast<StreamContext*>(Context);
    if (Event->Type == QUIC_STREAM_EVENT_RECEIVE) {
        for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) {
            ctx->buffer.insert(ctx->buffer.end(), Event->RECEIVE.Buffers[i].Buffer, Event->RECEIVE.Buffers[i].Buffer + Event->RECEIVE.Buffers[i].Length);
        }
        
        // 🔥 ÉP CHỜ NHẬN ĐỦ 100% GÓI TIN MỚI ĐƯỢC XỬ LÝ
        if (Event->RECEIVE.Flags & QUIC_RECEIVE_FLAG_FIN) {
            if (ctx->buffer.size() > 7 && memcmp(ctx->buffer.data(), "FS_CMD|", 7) == 0) {
                ctx->buffer.erase(ctx->buffer.begin(), ctx->buffer.begin() + 7);
            }
            const char* go_usr = go_get_username_by_ip(ctx->remote_ip.c_str());
            if (!go_usr) { 
                MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0); 
                return QUIC_STATUS_SUCCESS; 
            }
            std::string uds_path = "/tmp/zhiauth_kcp_" + std::string(go_usr) + ".sock";
            free((void*)go_usr);
            std::thread(&MsQuicServer::ProxyUdsTask, Stream, uds_path, ctx->buffer).detach();
        }
    } else if (Event->Type == QUIC_STREAM_EVENT_SEND_COMPLETE) {
        QUIC_BUFFER* qb = static_cast<QUIC_BUFFER*>(Event->SEND_COMPLETE.ClientContext);
        if (qb) { delete[] qb->Buffer; delete qb; }
    } else if (Event->Type == QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE) {
        delete ctx; MsQuic->StreamClose(Stream);
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API MsQuicServer::DataConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event) {
    if (Event->Type == QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED) {
        QUIC_ADDR RemoteAddr;
        uint32_t AddrLen = sizeof(RemoteAddr);
        MsQuic->GetParam(Connection, QUIC_PARAM_CONN_REMOTE_ADDRESS, &AddrLen, &RemoteAddr);
        char ip_str[INET6_ADDRSTRLEN] = {0};
        if (RemoteAddr.Ipv4.sin_family == AF_INET) inet_ntop(AF_INET, &(RemoteAddr.Ipv4.sin_addr), ip_str, INET_ADDRSTRLEN);
        else inet_ntop(AF_INET6, &(RemoteAddr.Ipv6.sin6_addr), ip_str, INET6_ADDRSTRLEN);
        
        std::string ip(ip_str);
        if (ip.find("::ffff:") == 0) ip = ip.substr(7);

        auto ctx = new StreamContext{{}, ip};
        MsQuic->SetCallbackHandler(Event->PEER_STREAM_STARTED.Stream, (void*)DataStreamCallback, ctx);
    } else if (Event->Type == QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE) {
        MsQuic->ConnectionClose(Connection);
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API MsQuicServer::DataListenerCallback(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event) {
    if (Event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, (void*)DataConnCallback, nullptr);
        MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
    }
    return QUIC_STATUS_SUCCESS;
}

QUIC_STATUS QUIC_API MsQuicServer::AuthListenerCallback(HQUIC Listener, void* Context, QUIC_LISTENER_EVENT* Event) {
    if (Event->Type == QUIC_LISTENER_EVENT_NEW_CONNECTION) {
        MsQuic->SetCallbackHandler(Event->NEW_CONNECTION.Connection, (void*)AuthConnCallback, nullptr);
        MsQuic->ConnectionSetConfiguration(Event->NEW_CONNECTION.Connection, Configuration);
    }
    return QUIC_STATUS_SUCCESS;
}
