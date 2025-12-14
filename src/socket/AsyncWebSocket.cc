#include "../../include/AsyncWebSocket.h"
#include "AsyncWebSocketClient.h"
#include "../request/AsyncWebServerRequest.h"
#include "../response/AsyncWebServerResponse.h"
#include "AsyncWebSocketMessageBuffer.h"
#include "../response/AsyncWebSocketResponse.h"
#include "../header/AsyncWebHeader.h"

#define TAG "AsyncWebSocket"

AsyncWebSocket::AsyncWebSocket(const std::string& uri)
    : buffers_(LinkedList<AsyncWebSocketMessageBuffer*>([](AsyncWebSocketMessageBuffer* b){ delete b; }))
    , enabled_(true)
    , next_id_(1)
    , uri_(uri)
    , eventHandler_(nullptr)
    , clients_(LinkedList<AsyncWebSocketClient*>([](AsyncWebSocketClient* c){ c->closeClient();}))  // 通知连接关闭
{
}

AsyncWebSocket::~AsyncWebSocket()
{
    buffers_.free();
    clients_.free();
}

/// @brief 检查请求是否为socket请求，是则为请求添加感兴趣头部
bool AsyncWebSocket::canHandle(AsyncWebServerRequest* req)
{
    if (enabled_ == false) {
        return false;
    }

    if (req->method_ != HTTP_GET
        || req->url_ != uri_
        || !req->isExpectedRequestedConnType(RCT_WS)) {
            return false;
    }
    req->addInterestingHeader(WS_STR_CONNECTION);
    req->addInterestingHeader(WS_STR_UPGRADE);
    req->addInterestingHeader(WS_STR_ORIGIN);
    req->addInterestingHeader(WS_STR_VERSION);
    req->addInterestingHeader(WS_STR_KEY);
    req->addInterestingHeader(WS_STR_PROTOCOL);
    return true;
}

/// @brief 处理请求，将协议升级为websocket(发送响应并收到确认后，会生成一个WebsocketClient对象接管连接)
/// @param req 
void AsyncWebSocket::handleRequest(AsyncWebServerRequest* req)
{
    // 检查请求头是否有Sec-WebSocket-Version及Sec-WebSocket-Key这2个必要请求头
    if (!req->hasHeader(WS_STR_VERSION) || !req->hasHeader(WS_STR_KEY)) {
        req->send(400); // 返回客户格式错误
        return;
    }
    // 进行socket客户端认证
    if (username_ != "" && password_ != "") {
        if (!req->authenticate(username_.c_str(), password_.c_str())) {
            return req->requestAuthentication();
        }
    }

    auto* version = req->getHeader(WS_STR_VERSION);
    if (std::stoi(version->value()) != 13)   {
        // 标准规定Sec-WebSocket-Version必须为13
        auto* response = req->beginResponse(400);
        response->addHeader(WS_STR_VERSION, "13");
        req->send(response);
        return;
    }

    auto* key = req->getHeader(WS_STR_KEY); // 获取握手时必须的key（客户端发送）
    auto* response = new AsyncWebSocketResponse(key->value(), this);
    if (req->hasHeader(WS_STR_PROTOCOL)) {
        auto* protocol = req->getHeader(WS_STR_PROTOCOL);
        response->addHeader(WS_STR_PROTOCOL, protocol->value());    // 添加支持的协议
    }
    req->send(response);
}

/// @brief 检查是否所有客户端可发送消息
bool AsyncWebSocket::availableForWriteAll()
{
    for (const auto& client : clients_) {
        if (client->queueIsFull()) {
            return false;
        }
    }
    return true;
}

/// @brief 检查指定的客户端是否可发送消息
bool AsyncWebSocket::availableForWrite(uint16_t id)
{
    for (const auto& c : clients_) {
        if (c->id() == id) {
            return !(c->queueIsFull());
        }
    }
    return false;
}

/// @brief 检查当前连接状态的客户端数量
size_t AsyncWebSocket::count() const
{
    return clients_.count_if([](AsyncWebSocketClient * c) {
        return c->status() == WS_CONNECTED;
    });
}

/// @brief 获取指定ID的客户端指针
AsyncWebSocketClient* AsyncWebSocket::client(uint16_t id)
{
    for (const auto &c : clients_) {
        if (c->id() == id && c->status() == WS_CONNECTED) {
            return c;
        }
    }
    return nullptr;
}

/// @brief 关闭指定的客户端
/// @param id 客户端ID
/// @param code 关闭代码
/// @param message 关闭消息
void AsyncWebSocket::close(uint16_t id, uint16_t code, const char* message)
{
    AsyncWebSocketClient* c = client(id);
    if (c != nullptr) {
        c->close(code, message);
    }
}

/// @brief 关闭所有客户代码
/// @param code 关闭代码
/// @param message 关闭消息
void AsyncWebSocket::closeAll(uint16_t code, const char* message)
{
    for (const auto &c : clients_) {
        if (c->status() == WS_CONNECTED) {
            c->close(code, message);
        }
    }
}

/// @brief 当客户端数量超过指定数量时，关闭一个客户端
void AsyncWebSocket::cleanupClients(uint16_t maxClients)
{
    if (count() > maxClients) {
        clients_.front()->close();
    }
}

/// @brief 主动指定ID客户端发送PING控制帧
/// @param id 客户端ID
/// @param data 携带的消息
/// @param len 消息的长度
void AsyncWebSocket::ping(uint16_t id, uint8_t* data, size_t len)
{
    auto* c = client(id);
    if (c != nullptr) {
        c->ping(data, len);
    }
}

/// @brief 向所有客户端发送PING控制帧
/// @param data 携带的消息
/// @param len 消息的长度
void AsyncWebSocket::pingAll(uint8_t* data, size_t len)
{
    for (const auto &c : clients_) {
        if (c->status() == WS_CONNECTED) {
            c->ping(data, len);
        }
    }
}

/// @brief 向指定客户端发送文本消息
void AsyncWebSocket::text(uint16_t id, const char* message)
{
    auto* c = client(id);
    if (c != nullptr) {
        c->text(message);
    }
}

/// @brief 向所有客户端广播文本消息
void AsyncWebSocket::textAll(const char* message)
{
    auto* wsBuffer = makeBuffer((uint8_t*)message, strlen(message));
    textAll(wsBuffer);
}

/// @brief 向所有客户端广播文本消息
void AsyncWebSocket::textAll(AsyncWebSocketMessageBuffer* buffer)
{
    if (buffer == nullptr) {
        return;
    }
    buffer->lock();
    for (const auto &c : clients_) {
        if (c->status() == WS_CONNECTED) {
            c->text(buffer);
        }
    }
    buffer->unlock();
    cleanBuffers();
}

/// @brief 向指定ID发送二进制帧（启用掩码）
void AsyncWebSocket::binary(uint16_t id, const char* message)
{
    auto* c = client(id);
    if (c != nullptr) {
        c->binary(message);
    }
}

/// @brief 向指定ID发送二进制帧（启用掩码）
void AsyncWebSocket::binary(uint16_t id, const std::string& message)
{
    binary(id, message.c_str());
}

/// @brief 向所有客户端发送二进制帧（启用掩码）
void AsyncWebSocket::binaryAll(const char* message)
{
    auto* buffer = makeBuffer((uint8_t*)message, strlen(message));
    binaryAll(buffer);
}

/// @brief 向所有客户端发送二进制帧（启用掩码）
void AsyncWebSocket::binaryAll(AsyncWebSocketMessageBuffer* buffer)
{
    if (buffer == nullptr) {
        return;
    }
    buffer->lock();
    for (const auto &c : clients_) {
        if (c->status() == WS_CONNECTED) {
            c->binary(buffer);
        }
    }
    buffer->unlock();
    cleanBuffers();
}

/// @brief 向指定ID发送原始消息
void AsyncWebSocket::message(uint16_t id, AsyncWebSocketMessage* message)
{
    auto* c = client(id);
    if (c) {
        c->message(message);
    }
}

/// @brief 向所有客户端发送原始消息
void AsyncWebSocket::messageAll(AsyncWebSocketMessage* message)
{
    for (const auto &c : clients_) {
        if (c->status() == WS_CONNECTED) {
            c->message(message);
        }
    }
    cleanBuffers();
}

/// @brief 将指定客户端添加到服务器的客户端列表中
void AsyncWebSocket::addClient(AsyncWebSocketClient* client)
{
    clients_.add(client);
}

/// @brief 移除客户端（执行定义的释放函数，移除客户端）
void AsyncWebSocket::cleanClient(AsyncWebSocketClient* client)
{
    auto id = client->id();
    clients_.remove_first([id](AsyncWebSocketClient * c) {
        return c->id() == id;
    });
}



/// @brief 清理可以删除的buffer
void AsyncWebSocket::cleanBuffers()
{
    AsyncWebLockGuard lock(lock_);
    for (auto* buffer : buffers_) {
        if (buffer && buffer->canDelete()) {
            buffers_.remove(buffer);
        }
    }
}

/// @brief 获取客户端列表
AsyncWebSocket::AsyncWebSocketClientLinkedList AsyncWebSocket::getClients() const
{
    return clients_;
}

/// @brief 构建一个指定大小的消息缓冲区
AsyncWebSocketMessageBuffer* AsyncWebSocket::makeBuffer(size_t size)
{
    auto* buffer = new AsyncWebSocketMessageBuffer(size);
    if (buffer) {
        AsyncWebLockGuard l(lock_);
        buffers_.add(buffer);
    }
    return buffer;
}

/// @brief 从给定的资源构建消息缓冲区
AsyncWebSocketMessageBuffer* AsyncWebSocket::makeBuffer(uint8_t* data, size_t size)
{
    auto* buffer = new AsyncWebSocketMessageBuffer(data, size);
    if (buffer) {
        AsyncWebLockGuard l(lock_);
        buffers_.add(buffer);
    }
    return buffer;
}