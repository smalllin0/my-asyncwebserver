#include "AsyncWebSocketResponse.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "../request/AsyncWebServerRequest.h"
#include "../socket/AsyncWebSocketClient.h"

AsyncWebSocketResponse::AsyncWebSocketResponse(const std::string& key, AsyncWebSocket* socket)
    : socket_(socket)
{
    code_ = 101;                // 选择协议
    sendContentLength_ = false;

    auto* hash = new uint8_t[20];
    if (hash == nullptr) {
        state_ = RESPONSE_FAILED;
        return;
    }

    auto* buffer = new char[33];
    if (buffer == nullptr) {
        delete[] hash;
        state_ = RESPONSE_FAILED;
        return;
    }

    (std::string &)key += std::string(WS_STR_UUID);
    mbedtls_sha1_context    ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, (const unsigned char*)key.c_str(), key.length());
    mbedtls_sha1_finish(&ctx, hash);
    mbedtls_sha1_free(&ctx);

    size_t encodeLength = 0;
    mbedtls_base64_encode((u_char*)buffer, 33, &encodeLength, hash, 20);
    addHeader(WS_STR_CONNECTION, WS_STR_UPGRADE);   // 添加响应头：声明协议升级
    addHeader(WS_STR_UPGRADE, "websocket");         // 
    addHeader(WS_STR_ACCEPT, buffer);               // 添加响应头：根据客户端key计算的响应值
    delete[] buffer;
    delete[] hash; 
}

//........这里用到了wirte()复制了数据，可以优化。当发送窗口大于要发送的数据量时完全不需要复制
void AsyncWebSocketResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ == RESPONSE_FAILED) {
        req->client_->close();
        return;
    }
    std::string out = assembleHead(req->version_);
    req->client_->write(out.c_str(), headLength_);
    state_ = RESPONSE_WAIT_ACK;
}

size_t AsyncWebSocketResponse::ack(AsyncWebServerRequest* req, size_t len, uint32_t time)
{
    if (len) {
        new AsyncWebSocketClient(req, socket_); // 创建websocket客户端对象
    }
    return 0;
}
