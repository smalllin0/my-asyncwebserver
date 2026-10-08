#include "AsyncWebSocketResponse.h"
#include "mbedtls/base64.h"
#include "psa/crypto.h"
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

    // PSA计算SHA-1 哈希
    psa_algorithm_t alg = PSA_ALG_SHA_1;
    const size_t hash_len = PSA_HASH_LENGTH(alg);
    size_t actual_len = 0;
    auto status = psa_hash_compute(
        alg,
        reinterpret_cast<const uint8_t*>(key.data()),
        key.size(),
        hash,
        20,
        &actual_len
    );
    if( status != PSA_SUCCESS || actual_len != hash_len) {
        state_ = RESPONSE_FAILED;
        return;
    }

    // Base64编码
    size_t encoded_len = 0;
    auto ret = mbedtls_base64_encode(
        reinterpret_cast<unsigned char*>(buffer),
        sizeof(buffer),
        &encoded_len,
        hash,
        actual_len
    );
    if (ret != 0 || encoded_len >= sizeof(buffer)) {
        state_ = RESPONSE_FAILED;
        return;
    }
    buffer[encoded_len] = '\0'; // 确保字符串终止


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
    req->client_->add(out.c_str(), headLength_);
    state_ = RESPONSE_WAIT_ACK;
}

size_t AsyncWebSocketResponse::ack(AsyncWebServerRequest* req, size_t len, uint32_t time)
{
    if (len) {
        new AsyncWebSocketClient(req, socket_); // 创建websocket客户端对象
    }
    return 0;
}
