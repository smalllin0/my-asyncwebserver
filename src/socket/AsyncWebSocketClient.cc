// =============================================================
// 文件：AsyncWebSocketClient.cc
// =============================================================
#include "AsyncWebSocketClient.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncWebSocket.h"
#include "AsyncWebServer.h"
#include "AsyncWebSocketControl.h"
#include "AsyncWebSocketMessage.h"
#include "my_sysInfo.h"
#include <algorithm>
#include <lwip/inet.h>

#define TAG "AsyncWebSocketClient"
#define AWSC_PING_PAYLOAD   "AsyncSocket-PING"

// =============================================================
// 构造 / 析构
// =============================================================

AsyncWebSocketClient::AsyncWebSocketClient(AsyncWebServerRequest* req, AsyncWebSocket* socket)
    : controlQueue_(LinkedList<AsyncWebSocketControl*>([](AsyncWebSocketControl* c){ delete c; }))
    , messageQueue_(LinkedList<AsyncWebSocketMessage*>([](AsyncWebSocketMessage* m){ delete m; }))
{
    // 接管底层连接（AsyncWebSocketClient 是 AsyncWebServerRequest 的友元）
    client_ = req->client_;
    req->client_ = nullptr;         // 断开 request 对连接的引用

    socket_ = socket;
    id_ = socket_->getNextId();
    status_ = WS_CONNECTED;
    pstate_ = WS_PARSE_HEAD;
    lastMessageTime_ = SystemInfo::GetMsSinceStart();
    keepAlivePeriod_ = 0;

    client_->SetRxTimeout(0);

    // 数据到达
    client_->OnData([](void* ctx, void* buf, size_t len) {
        static_cast<AsyncWebSocketClient*>(ctx)->onData(buf, len);
    }, this);

    // 错误
    client_->OnError([](void* ctx, int8_t error) {
        auto* self = static_cast<AsyncWebSocketClient*>(ctx);
        ESP_LOGE(TAG, "Client error: id=%u, error=%d", self->id_, error);
        self->socket_->handleEvent(self, WS_EVT_ERROR, &error, nullptr, 0);
    }, this);

    // 周期轮询（驱动队列 + 心跳）
    client_->OnPoll([](void* ctx) {
        static_cast<AsyncWebSocketClient*>(ctx)->onPoll();
    }, this);

    // 回收前清理：通知 socket 移除自己
    client_->OnRelease([](void* ctx) {
        auto* self = static_cast<AsyncWebSocketClient*>(ctx);
        self->socket_->cleanClient(self);
        delete self;                                // AsyncConnection 回收时销毁 WebSocketClient
    }, this);

    // ★ 不再注册 OnSent / OnTimeout / OnDisconnect（新模型下无效）
    //   帧 ACK 由 chunk cb 直接驱动消息对象；连接断开由 OnRelease 统一处理

    socket_->addClient(this);
    socket_->handleEvent(this, WS_EVT_CONNECT, req, nullptr, 0);

    // request 已交给 WebSocket 接管，回收它
    req->server_->recycleRequest(req);
}

AsyncWebSocketClient::~AsyncWebSocketClient()
{
    if (client_) {
        // 清空所有回调，防止 disconnect 后收到事件
        client_->OnData(nullptr, nullptr);
        client_->OnError(nullptr, nullptr);
        client_->OnPoll(nullptr, nullptr);
        client_->OnRelease(nullptr, nullptr);
    }
    socket_->handleEvent(this, WS_EVT_DISCONNECT, nullptr, nullptr, 0);
    messageQueue_.free();
    controlQueue_.free();
}


// =============================================================
// 队列处理
// =============================================================

/// @brief 添加消息帧到队列并尝试发送
void AsyncWebSocketClient::queueMessage(AsyncWebSocketMessage* message)
{
    if (!message) return;

    if (status_ != WS_CONNECTED) {
        delete message;
        return;
    }

    if (messageQueue_.length() >= CONFIG_WS_MAX_QUEUE_MESSAGES) {
        delete message;
        ESP_LOGW(TAG, "Message reject, queue is full.");
        return;
    }

    messageQueue_.add(message);
    runQueue();                     // ★ 无条件尝试发送
}

/// @brief 添加控制帧到队列并尝试发送
void AsyncWebSocketClient::queueControl(AsyncWebSocketControl* control)
{
    if (!control) return;
    controlQueue_.add(control);
    runQueue();                     // ★ 无条件尝试发送
}

/// @brief 按优先级发送控制帧 / 消息帧
/// @note 每次最多入队一个帧，避免单连接占满 SendPool
void AsyncWebSocketClient::runQueue()
{
    if (status_ == WS_DISCONNECTED) return;

    // 清理已发送完成的控制帧
    // （控制帧 send() 返回后即标记 finished，可以从队列移除）
    while (!controlQueue_.isEmpty() && controlQueue_.front()->finished()) {
        controlQueue_.remove(controlQueue_.front());
    }

    // 清理已完成的消息帧
    while (!messageQueue_.isEmpty() && messageQueue_.front()->finished()) {
        messageQueue_.remove(messageQueue_.front());
    }

    // ---- 控制帧优先 ----
    // 仅当消息帧处于帧间态（不在等 ACK）时才发控制帧
    if (!controlQueue_.isEmpty()
        && (messageQueue_.isEmpty() || messageQueue_.front()->betweenFrames()))
    {
        auto* ctrl = controlQueue_.front();
        if (ctrl->send(client_) > 0) {
            // 成功入队，从队列移除
            // 特殊：如果状态是 WS_DISCONNECTING 且刚发了 DISCONNECT，让发送完成后再改状态
            if (status_ == WS_DISCONNECTING && ctrl->opcode() == WS_DISCONNECT) {
                // 等待 OnRelease 触发，不在这里改状态
            }
            controlQueue_.remove(ctrl);
        }
        return;
    }

    // ---- 消息帧 ----
    if (!messageQueue_.isEmpty() && messageQueue_.front()->betweenFrames()) {
        messageQueue_.front()->send(client_);
    }
}


// =============================================================
// 事件回调
// =============================================================

/// @brief 周期轮询：驱动队列 + 发送心跳
/// @note 由 AsyncConnection::PollTask 在后台线程触发（1 秒 1 次）
inline void AsyncWebSocketClient::onPoll()
{
    if (status_ == WS_DISCONNECTED) return;

    // 队列里有东西就尝试推进
    if (!controlQueue_.isEmpty() || !messageQueue_.isEmpty()) {
        runQueue();
        return;
    }

    // 队列空 + 心跳周期到 → 发 PING
    if (keepAlivePeriod_ && controlQueue_.isEmpty() && messageQueue_.isEmpty()
        && SystemInfo::Timeout(lastMessageTime_, keepAlivePeriod_))
    {
        ping(reinterpret_cast<uint8_t*>(const_cast<char*>(AWSC_PING_PAYLOAD)),
             sizeof(AWSC_PING_PAYLOAD));
    }
}

/// @brief 由 AsyncConnection 回收时调用（OnRelease 里已处理）
inline void AsyncWebSocketClient::onFree()
{
    socket_->cleanClient(this);
}


// =============================================================
// 数据帧解析（与旧版逻辑一致，只保留必要修改）
// =============================================================

inline void AsyncWebSocketClient::onData(void* pbuf, size_t buf_len)
{
    lastMessageTime_ = SystemInfo::GetMsSinceStart();
    auto* data = (uint8_t*)pbuf;

    while (buf_len > 0) {
        if (pstate_ == WS_PARSE_HEAD) {
            auto frameHeaderCacheLen = frameHeaderCache_.length();
            auto total_len = buf_len + frameHeaderCacheLen;

            uint8_t second_byte = 0;
            if (frameHeaderCacheLen) {
                second_byte = frameHeaderCacheLen == 1 ? data[0] : (uint8_t)frameHeaderCache_[1];
            } else if (buf_len >= 2) {
                second_byte = data[1];
            }

            // 判断帧头是否完整
            if (((total_len >= 2) && ((second_byte & 0x7f) < 126) && ((second_byte & 0x80) == 0))
             || ((total_len >= 6) && ((second_byte & 0x7f) < 126))
             || ((total_len >= 4) && ((second_byte & 0x7f) == 126) && ((second_byte & 0x80) == 0))
             || ((total_len >= 6) && ((second_byte & 0x7f) == 126))
             || ((total_len >= 10) && ((second_byte & 0x7f) == 127) && ((second_byte & 0x80) == 0))
             || ((total_len >= 14) && ((second_byte & 0x7f) == 127)))
            {
                auto first_byte = frameHeaderCacheLen ? frameHeaderCache_[0] : data[0];
                pinfo_.index = 0;
                pinfo_.len = 0;
                pinfo_.final = (first_byte & 0x80) != 0;
                pinfo_.opcode = first_byte & 0x0f;
                pinfo_.masked = (second_byte & 0x80) != 0;
                auto masked = pinfo_.masked;
                auto len = second_byte & 0x7f;

                if (len < 126) {
                    pinfo_.len = len;
                    if (masked) {
                        if (frameHeaderCacheLen <= 2) {
                            memcpy(pinfo_.mask, data + 2 - frameHeaderCacheLen, 4);
                        } else {
                            memcpy(pinfo_.mask, frameHeaderCache_.data() + 2, frameHeaderCacheLen - 2);
                            memcpy(pinfo_.mask + (frameHeaderCacheLen - 2), data, 4 - (frameHeaderCacheLen - 2));
                        }
                    }
                    auto step = masked ? 6 - frameHeaderCacheLen : 2 - frameHeaderCacheLen;
                    data += step;
                    buf_len -= step;
                } else if (len == 126) {
                    if (frameHeaderCacheLen <= 2) {
                        pinfo_.len = data[2 - frameHeaderCacheLen] << 8 | data[3 - frameHeaderCacheLen];
                    } else if (frameHeaderCacheLen == 3) {
                        pinfo_.len = frameHeaderCache_[2] << 8 | data[0];
                    } else {
                        pinfo_.len = frameHeaderCache_[2] << 8 | frameHeaderCache_[3];
                    }
                    if (masked) {
                        if (frameHeaderCacheLen <= 4) {
                            memcpy(pinfo_.mask, data + 4 - frameHeaderCacheLen, 4);
                        } else if (frameHeaderCacheLen < 8) {
                            memcpy(pinfo_.mask, frameHeaderCache_.data() + 4, frameHeaderCacheLen - 4);
                            memcpy(pinfo_.mask + frameHeaderCacheLen - 4, data + 4 - frameHeaderCacheLen, 8 - frameHeaderCacheLen);
                        }
                    }
                    auto step = masked ? 8 - frameHeaderCacheLen : 4 - frameHeaderCacheLen;
                    data += step;
                    buf_len -= step;
                } else {
                    uint32_t tmp[2] = {0};
                    if (frameHeaderCacheLen <= 2) {
                        memcpy(tmp, data + 2 - frameHeaderCacheLen, 8);
                    } else if (frameHeaderCacheLen < 9) {
                        memcpy(tmp, frameHeaderCache_.data() + 2, frameHeaderCacheLen - 2);
                        memcpy((uint8_t*)tmp + frameHeaderCacheLen - 2, data, 8 - (frameHeaderCacheLen - 2));
                    } else {
                        memcpy(tmp, frameHeaderCache_.data() + 2, 8);
                    }
                    if (tmp[0]) {
                        ESP_LOGE(TAG, "Unsupported 64bit length, closing socket.");
                        client_->Close();
                    } else {
                        pinfo_.len = ntohl(tmp[1]);
                    }
                    if (masked) {
                        if (frameHeaderCacheLen <= 10) {
                            memcpy(pinfo_.mask, data + 10 - frameHeaderCacheLen, 4);
                        } else if (frameHeaderCacheLen < 13) {
                            memcpy(pinfo_.mask, data + 10 - frameHeaderCacheLen, frameHeaderCacheLen - 10);
                            memcpy(pinfo_.mask + frameHeaderCacheLen - 10, data, 4 - (frameHeaderCacheLen - 10));
                        }
                    }
                    auto step = masked ? 14 - frameHeaderCacheLen : 10 - frameHeaderCacheLen;
                    data += step;
                    buf_len -= step;
                }
                pstate_ = WS_PARSE_PAYLOAD;
            } else {
                frameHeaderCache_.append((const char*)data, buf_len);
            }
        }

        if (pstate_ == WS_PARSE_PAYLOAD) {
            const size_t data_len = std::min((size_t)(pinfo_.len - pinfo_.index), buf_len);

            if (pinfo_.masked) {
                for (size_t i = 0; i < data_len; i++) {
                    data[i] ^= pinfo_.mask[(pinfo_.index + i) & 0x03];
                }
            }

            if ((data_len + pinfo_.index) < pinfo_.len) {
                // 帧未处理完成
                if (pinfo_.opcode == 0) {
                    pinfo_.num++;
                } else {
                    pinfo_.num = 0;
                    pinfo_.message_opcode = pinfo_.opcode;
                }
                socket_->handleEvent(this, WS_EVT_DATA, &pinfo_, data, data_len);
                pinfo_.index += data_len;
            } else if ((data_len + pinfo_.index) == pinfo_.len) {
                // 帧处理完成
                switch (pinfo_.opcode) {
                  case WS_DISCONNECT:
                    if (data_len) {
                        uint16_t reason_code = (data[0] << 8) | data[1];
                        char* reason = (char*)(data + 2);
                        if (reason_code > 1001) {
                            socket_->handleEvent(this, WS_EVT_ERROR, &reason_code,
                                                 (uint8_t*)reason, strlen(reason));
                        }
                    }
                    if (status_ == WS_DISCONNECTING) {
                        status_ = WS_DISCONNECTED;
                        if (client_) client_->Close();
                    } else {
                        status_ = WS_DISCONNECTING;
                        queueControl(new AsyncWebSocketControl(WS_DISCONNECT, data, data_len));
                    }
                    break;
                  case WS_PING:
                    queueControl(new AsyncWebSocketControl(WS_PONG, data, data_len));
                    break;
                  case WS_PONG:
                    if (data_len != sizeof(AWSC_PING_PAYLOAD)
                        || memcmp(AWSC_PING_PAYLOAD, data, sizeof(AWSC_PING_PAYLOAD)) != 0) {
                        socket_->handleEvent(this, WS_EVT_PONG, nullptr, data, data_len);
                    }
                    break;
                  default:
                    socket_->handleEvent(this, WS_EVT_DATA, &pinfo_, data, data_len);
                }
                pstate_ = WS_PARSE_HEAD;
            } else {
                ESP_LOGE(TAG, "WebSocket frame parse error, closing.");
                Close();
            }
            data += data_len;
            buf_len -= data_len;
        }
    }
}


// =============================================================
// 主动操作
// =============================================================

/// @brief 发送关闭帧并断开连接
void AsyncWebSocketClient::Close(uint16_t code, const char* message)
{
    if (status_ != WS_CONNECTED) return;

    if (code) {
        uint8_t packet_len = 2;
        if (message != nullptr) {
            size_t message_len = strlen(message);
            packet_len = std::min((size_t)125, 2 + message_len);
        }
        auto* buf = new (std::nothrow) char[packet_len];
        if (buf != nullptr) {
            buf[0] = (uint8_t)(code >> 8);
            buf[1] = (uint8_t)(code & 0xff);
            if (message != nullptr) {
                memcpy(buf + 2, message, packet_len - 2);
            }
            queueControl(new AsyncWebSocketControl(WS_DISCONNECT, (uint8_t*)buf, packet_len));
            delete[] buf;
            return;
        }
    }
    queueControl(new AsyncWebSocketControl(WS_DISCONNECT));
}

/// @brief 发送 PING 帧
void AsyncWebSocketClient::ping(uint8_t* data, size_t len)
{
    if (status_ == WS_CONNECTED) {
        queueControl(new AsyncWebSocketControl(WS_PING, data, len));
    }
}

/// @brief 消息队列是否已满
inline bool AsyncWebSocketClient::queueIsFull()
{
    return (messageQueue_.length() >= CONFIG_WS_MAX_QUEUE_MESSAGES)
        || (status_ != WS_CONNECTED);
}


// =============================================================
// 信息查询
// =============================================================

ip_addr_t AsyncWebSocketClient::remoteIP()
{
    if (client_ == nullptr) {
        return IPADDR4_INIT(0);
    }
    return client_->GetRemouteIp();
}

uint16_t AsyncWebSocketClient::remotePort()
{
    return client_ == nullptr ? 0 : client_->GetRemotePort();
}