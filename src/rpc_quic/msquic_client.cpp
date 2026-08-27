#include "rpc_quic/msquic_client.h"
#include <cstring>
#include <unordered_map>
#include <mutex>
#include "common/logger.h"

const QUIC_BUFFER ALPN_BUFFER = { sizeof("zhiauth-rpc") - 1, (uint8_t*)"zhiauth-rpc" };
static const QUIC_API_TABLE* MsQuic = nullptr;
static HQUIC Registration = nullptr;
static HQUIC Configuration = nullptr;
static HQUIC AuthConnection = nullptr;
static HQUIC DataConnection = nullptr;

static std::mutex g_msquic_rpc_mtx;
static std::unordered_map<uint32_t, std::shared_ptr<std::promise<std::vector<uint8_t>>>> g_msquic_rpc_map;

struct RpcClientContext { uint32_t req_id; std::vector<uint8_t> resData; QUIC_BUFFER* qBuf; };
struct AuthClientContext { std::promise<std::string> promise; std::string resData; QUIC_BUFFER* qBuf; };

QUIC_STATUS QUIC_API MsQuicClient::ClientConnCallback(HQUIC Connection, void* Context, QUIC_CONNECTION_EVENT* Event) { return QUIC_STATUS_SUCCESS; }

bool MsQuicClient::initialize(const std::string& server_ip, uint16_t auth_port, uint16_t data_port) {
    if (QUIC_FAILED(MsQuicOpen2(&MsQuic))) return false;
    const QUIC_REGISTRATION_CONFIG RegConfig = { "ZhiAuthClient", QUIC_EXECUTION_PROFILE_LOW_LATENCY };
    MsQuic->RegistrationOpen(&RegConfig, &Registration);
    
    QUIC_SETTINGS Settings; memset(&Settings, 0, sizeof(Settings));
    Settings.IdleTimeoutMs = 120000; Settings.IsSet.IdleTimeoutMs = 1;
    // 🔥 THUỐC CHỐNG NGỦ GẬT NẰM Ở ĐÂY: Ép Client tự bắn Ping mỗi 15 giây
    Settings.KeepAliveIntervalMs = 15000; Settings.IsSet.KeepAliveIntervalMs = 1;
    
    MsQuic->ConfigurationOpen(Registration, &ALPN_BUFFER, 1, &Settings, sizeof(Settings), nullptr, &Configuration);
    QUIC_CREDENTIAL_CONFIG CredConfig; memset(&CredConfig, 0, sizeof(CredConfig));
    CredConfig.Type = QUIC_CREDENTIAL_TYPE_NONE; CredConfig.Flags = QUIC_CREDENTIAL_FLAG_CLIENT | QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION;
    MsQuic->ConfigurationLoadCredential(Configuration, &CredConfig);
    MsQuic->ConnectionOpen(Registration, ClientConnCallback, nullptr, &AuthConnection);
    MsQuic->ConnectionStart(AuthConnection, Configuration, QUIC_ADDRESS_FAMILY_UNSPEC, server_ip.c_str(), auth_port);
    MsQuic->ConnectionOpen(Registration, ClientConnCallback, nullptr, &DataConnection);
    MsQuic->ConnectionStart(DataConnection, Configuration, QUIC_ADDRESS_FAMILY_UNSPEC, server_ip.c_str(), data_port);
    return true;
}

void MsQuicClient::shutdown() {
    if (AuthConnection) MsQuic->ConnectionClose(AuthConnection);
    if (DataConnection) MsQuic->ConnectionClose(DataConnection);
    if (Configuration) MsQuic->ConfigurationClose(Configuration);
    if (Registration) MsQuic->RegistrationClose(Registration);
    if (MsQuic) MsQuicClose(MsQuic);
}

QUIC_STATUS QUIC_API MsQuicClient::AuthStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    auto* ctx = static_cast<AuthClientContext*>(Context);
    switch (Event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) ctx->resData.append(reinterpret_cast<char*>(Event->RECEIVE.Buffers[i].Buffer), Event->RECEIVE.Buffers[i].Length);
            break;
        }
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            if (ctx && ctx->qBuf) { delete[] ctx->qBuf->Buffer; delete ctx->qBuf; ctx->qBuf = nullptr; } break;
        }
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: ctx->promise.set_value(ctx->resData); break;
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: MsQuic->StreamClose(Stream); delete ctx; break;
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

std::string MsQuicClient::auth_sync(const std::string& payload) {
    auto* ctx = new AuthClientContext(); auto fut = ctx->promise.get_future();
    HQUIC Stream = nullptr;
    MsQuic->StreamOpen(AuthConnection, QUIC_STREAM_OPEN_FLAG_NONE, AuthStreamCallback, ctx, &Stream);
    MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE);
    ctx->qBuf = new QUIC_BUFFER; ctx->qBuf->Buffer = new uint8_t[payload.size()]; memcpy(ctx->qBuf->Buffer, payload.data(), payload.size()); ctx->qBuf->Length = payload.size();
    MsQuic->StreamSend(Stream, ctx->qBuf, 1, QUIC_SEND_FLAG_FIN, ctx->qBuf);
    if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready) return fut.get();
    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
    return "AUTH_FAILED";
}

QUIC_STATUS QUIC_API MsQuicClient::RpcStreamCallback(HQUIC Stream, void* Context, QUIC_STREAM_EVENT* Event) {
    auto* ctx = static_cast<RpcClientContext*>(Context);
    switch (Event->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            for (uint32_t i = 0; i < Event->RECEIVE.BufferCount; ++i) ctx->resData.insert(ctx->resData.end(), Event->RECEIVE.Buffers[i].Buffer, Event->RECEIVE.Buffers[i].Buffer + Event->RECEIVE.Buffers[i].Length);
            break;
        }
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            if (ctx && ctx->qBuf) { delete[] ctx->qBuf->Buffer; delete ctx->qBuf; ctx->qBuf = nullptr; } break;
        }
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
        case QUIC_STREAM_EVENT_PEER_RECEIVE_ABORTED:
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN: {
            std::shared_ptr<std::promise<std::vector<uint8_t>>> prom;
            {
                std::lock_guard<std::mutex> rpc_lock(g_msquic_rpc_mtx);
                auto it = g_msquic_rpc_map.find(ctx->req_id);
                if (it != g_msquic_rpc_map.end()) { prom = it->second; g_msquic_rpc_map.erase(it); }
            }
            if (prom) prom->set_value(ctx->resData);
            break;
        }
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE: MsQuic->StreamClose(Stream); delete ctx; break;
        default: break;
    }
    return QUIC_STATUS_SUCCESS;
}

std::vector<uint8_t> MsQuicClient::send_vfs_sync(const std::vector<uint8_t>& payload, uint32_t req_id) {
    auto prom = std::make_shared<std::promise<std::vector<uint8_t>>>();
    auto fut = prom->get_future();
    { std::lock_guard<std::mutex> lock(g_msquic_rpc_mtx); g_msquic_rpc_map[req_id] = prom; }

    auto* ctx = new RpcClientContext{req_id, {}, nullptr};
    HQUIC Stream = nullptr;
    MsQuic->StreamOpen(DataConnection, QUIC_STREAM_OPEN_FLAG_NONE, RpcStreamCallback, ctx, &Stream);
    MsQuic->StreamStart(Stream, QUIC_STREAM_START_FLAG_NONE);

    ctx->qBuf = new QUIC_BUFFER; ctx->qBuf->Buffer = new uint8_t[payload.size()]; memcpy(ctx->qBuf->Buffer, payload.data(), payload.size()); ctx->qBuf->Length = payload.size();
    MsQuic->StreamSend(Stream, ctx->qBuf, 1, QUIC_SEND_FLAG_FIN, ctx->qBuf);

    if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready) return fut.get();
    
    ZHI_LOG_ERR("[RPC-TIMEOUT] MsQUIC đứt gánh với ReqID: " + std::to_string(req_id));
    MsQuic->StreamShutdown(Stream, QUIC_STREAM_SHUTDOWN_FLAG_ABORT, 0);
    { std::lock_guard<std::mutex> lock(g_msquic_rpc_mtx); g_msquic_rpc_map.erase(req_id); }
    return {};
}
