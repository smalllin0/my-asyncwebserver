// AsyncWebSocketResponse.cc
#include "AsyncWebSocketResponse.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
#include "../request/AsyncWebServerRequest.h"
#include "../socket/AsyncWebSocketClient.h"

AsyncWebSocketResponse::AsyncWebSocketResponse(const std::string& key, AsyncWebSocket* socket)
    : socket_(socket)
{
    code_ = 101;
    sendContentLength_ = false;

    auto* hash = new (std::nothrow) uint8_t[20];
    if (!hash) { state_ = RESPONSE_FAILED; return; }

    auto* buffer = new (std::nothrow) char[33];
    if (!buffer) { delete[] hash; state_ = RESPONSE_FAILED; return; }

    std::string k = key + WS_STR_UUID;

    size_t actual_len = 0;
    auto status = psa_hash_compute(
        PSA_ALG_SHA_1,
        reinterpret_cast<const uint8_t*>(k.data()),
        k.size(),
        hash, 20, &actual_len);
    if (status != PSA_SUCCESS) {
        delete[] hash; delete[] buffer;
        state_ = RESPONSE_FAILED;
        return;
    }

    size_t encoded_len = 0;
    auto ret = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(buffer),
        33, &encoded_len, hash, actual_len);
    delete[] hash;
    if (ret != 0 || encoded_len >= 33) {
        delete[] buffer;
        state_ = RESPONSE_FAILED;
        return;
    }
    buffer[encoded_len] = '\0';

    addHeader(WS_STR_CONNECTION, WS_STR_UPGRADE);
    addHeader(WS_STR_UPGRADE,    "websocket");
    addHeader(WS_STR_ACCEPT,     buffer);
    delete[] buffer;
}

void AsyncWebSocketResponse::doRespond()
{
    if (state_ == RESPONSE_FAILED) {
        req_->close();
        return;
    }

    state_ = RESPONSE_WAIT_ACK;
    req_->append(header_.c_str(), header_.length(), TCP_WRITE_FLAG_COPY);

    req_->OnDrain([](void* ctx) {
        auto* self = static_cast<AsyncWebSocketResponse*>(ctx);
        self->req_->OnDrain(nullptr, nullptr);      // 清回调，转入 WebSocketClient
        new AsyncWebSocketClient(self->req_, self->socket_);
        self->state_ = RESPONSE_END;
        // ★ 不 finish：WebSocket 长连接，response 由 request 生命周期持有
    }, this);

    req_->flush();
}