#include "AsyncWebSocketClient.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncWebSocket.h"
#include "AsyncWebServer.h"
#include "AsyncWebSocketControl.h"
#include "AsyncWebSocketMessage.h"
#include "my_sysInfo.h"
#include <algorithm>
#include "sys/_stdint.h"
#include <lwip/inet.h>


#define TAG "AsyncWebSocketClient"
#define AWSC_PING_PAYLOAD   "AsyncSocket-PING"

AsyncWebSocketClient::AsyncWebSocketClient(AsyncWebServerRequest* req, AsyncWebSocket* socket)
    : controlQueue_(LinkedList<AsyncWebSocketControl*>([](AsyncWebSocketControl* contrl){ delete contrl; }))
    , messageQueue_(LinkedList<AsyncWebSocketMessage*>([](AsyncWebSocketMessage* msg){ delete msg; }))
{
    client_ = req->client_;
    req->client_ = nullptr;         // 注销关联底层tcp连接
    socket_ = socket;
    id_ = socket_->getNextId();
    status_ = WS_CONNECTED;
    pstate_ = WS_PARSE_HEAD;            
    lastMessageTime_ = SystemInfo::GetMsSinceStart();
    keepAlivePeriod_ = 0;
    client_->set_rx_timeout_second(0);
    client_->set_data_received_handler([](void* client, void* buf, size_t len) {
            ((AsyncWebSocketClient*)client)->onData(buf, len);
        },
        this
    );
    client_->set_error_event_handler([](void* arg, err_t error) {
            auto* client = reinterpret_cast<AsyncWebSocketClient*>(arg);
            ESP_LOGE(TAG, "Client error: client id=%d, error=%s.", client->id_, esp_err_to_name(error));
        }, this);

    client_->set_ack_event_handler([](void* client, size_t len, uint32_t time) {
            ((AsyncWebSocketClient*)client)->onAck(len, time);
        },
        this
    );

    // 重围断开业务处理回调，（由于接管req，必须重围）
    client_->set_disconnected_event_handler(nullptr, nullptr);
    // 超时将关闭底层连接、销毁本对象
    client_->set_timeout_event_handler([](void* arg, uint32_t time) {
            auto* self = (AsyncWebSocketClient*)arg;
            self->socket_->handleEvent(self, WS_EVT_DISCONNECT, nullptr, nullptr, 0);
            self->messageQueue_.free();
            self->controlQueue_.free();
            auto* client = (AsyncClient*)self;
            client->close();
        },
        this
    );
    client_->set_poll_event_handler([](void* client) {
            ((AsyncWebSocketClient*)client)->onPoll();
        },
        this
    );
    // 重置回收回调----不回收socket对象
    client_->set_recycle_handler(nullptr, nullptr);
    
    socket_->addClient(this);
    socket_->handleEvent(this, WS_EVT_CONNECT, req, nullptr, 0);
    req->server_->recycleRequest(req);
}

AsyncWebSocketClient::~AsyncWebSocketClient()
{
    if (client_) {
        client_->set_error_event_handler(nullptr, nullptr);
        client_->set_ack_event_handler(nullptr, nullptr);
        client_->set_disconnected_event_handler(nullptr, nullptr);
        client_->set_timeout_event_handler(nullptr, nullptr);
        client_->set_data_received_handler(nullptr, nullptr);
        client_->set_poll_event_handler(nullptr, nullptr);
    }
    socket_->handleEvent(this, WS_EVT_DISCONNECT, nullptr, nullptr, 0);
    messageQueue_.free();
    controlQueue_.free();
}

inline void AsyncWebSocketClient::onAck(size_t len, uint32_t time)
{
    lastMessageTime_ = SystemInfo::GetMsSinceStart();
    if (controlQueue_.length()) {
        auto head = controlQueue_.front();
        if (head->finished()) {
            len -= head->len();
            if (status_ == WS_DISCONNECTING && head->opcode() == WS_DISCONNECT) {
                controlQueue_.remove(head);
                status_ = WS_DISCONNECTED;
                client_->close();
                return;
            }
            controlQueue_.remove(head);
        }
    }
    if (len && messageQueue_.length()) {
        messageQueue_.front()->ack(len, time);
    }
    socket_->cleanBuffers();
    runQueue();
}

inline void AsyncWebSocketClient::onFree()
{
    socket_->cleanClient(this);
}


/// @brief 用于接管原来HTTP连接的数据到达处理程序
inline void AsyncWebSocketClient::onData(void* pbuf, size_t buf_len)
{
    lastMessageTime_ = SystemInfo::GetMsSinceStart();
    auto* data = (uint8_t*) pbuf;
    while (buf_len > 0) {
        if (pstate_ == WS_PARSE_HEAD) {
            auto frameHeaderCacheLen = frameHeaderCache_.length();
            auto total_len = buf_len + frameHeaderCacheLen;
    

            // 获取帧长信息字节
            uint8_t second_byte = 0;
            if (frameHeaderCacheLen) {
                // 存在缓存的关部信息
                second_byte = frameHeaderCacheLen == 1 ? data[0] : (uint8_t)frameHeaderCache_[1];
            } else if (buf_len >= 2) {
                second_byte = data[1];
            }

            // 判断是否完整的帧头，获取数据的真实起始地址
            if (((total_len >= 2)&&((second_byte & 0x7f) < 126)&&((second_byte & 0x80) == 0))   // 126长度以下无掩码
            || ((total_len >= 6)&&((second_byte & 0x7f) < 126))                                 // 126长度以下
            || ((total_len >= 4)&&((second_byte & 0x7f) == 126)&&((second_byte & 0x80) == 0))   // 扩展长度2位，无掩码
            || ((total_len >= 6)&&((second_byte & 0x7f) == 126))                                // 扩展长度2位
            || ((total_len >= 10)&&((second_byte & 0x7f) == 127)&&((second_byte & 0x80) == 0))  // 扩展长度8位，无掩码
            || ((total_len >= 14)&&((second_byte & 0x7f) == 127))) {                            // 扩展长度8位                        
            
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
                } else if (len == 126) {    // 4or8位长度
                    // 解析长度
                    if (frameHeaderCacheLen <= 2) {
                        pinfo_.len = data[2 - frameHeaderCacheLen] << 8 | data[3 - frameHeaderCacheLen];
                    } else if (frameHeaderCacheLen == 3) {
                        pinfo_.len = frameHeaderCache_[2] << 8 | data[0];
                    } else {
                        pinfo_.len = frameHeaderCache_[2] << 8 | frameHeaderCache_[3];
                    }

                    // 解析掩码
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
                    // 解析长度
                    uint32_t tmp[2] = {0};
                    if (frameHeaderCacheLen <= 2) { // 长度从data解析
                        memcpy(tmp, data + 2 - frameHeaderCacheLen, 8);
                    } else if (frameHeaderCacheLen < 9) {
                        memcpy(tmp, frameHeaderCache_.data() + 2, frameHeaderCacheLen - 2);
                        memcpy((uint8_t*)tmp + frameHeaderCacheLen - 2, data, 8 - (frameHeaderCacheLen - 2));
                    } else {
                        memcpy(tmp, frameHeaderCache_.data() + 2, 8);
                    }
                    if (tmp[0]) {
                        ESP_LOGE(TAG, "Unsupport 64bit lenght, socket will close.");
                        client_->close();   //..........................构造关闭帧
                    } else {    
                        pinfo_.len = ntohl(tmp[1]);
                    }


                    // 获取掩码
                    if (masked) {
                        if (frameHeaderCacheLen <= 10) {
                            memcpy(pinfo_.mask, data + 10 - frameHeaderCacheLen, 4);
                        } else if (frameHeaderCacheLen < 13) {
                            memcpy(pinfo_.mask, data + 10 - frameHeaderCacheLen, frameHeaderCacheLen - 10);
                            memcpy(pinfo_.mask + frameHeaderCacheLen - 10, data, 4 - (frameHeaderCacheLen - 10));
                        }
                    }

                    // 变换长度
                    auto step = masked ? 14 - frameHeaderCacheLen : 10 - frameHeaderCacheLen;
                    data += step;
                    buf_len -= step;
                }
                pstate_ = WS_PARSE_PAYLOAD;
            } else {
                // 保存不完整的帧头
                frameHeaderCache_.append((const char*)data, buf_len);  
            }
        }
        


        if (pstate_ == WS_PARSE_PAYLOAD) {
            // 当前帧可处理的长度
            const size_t data_len = std::min((size_t)(pinfo_.len - pinfo_.index), buf_len);

            if (pinfo_.masked) {
                for (size_t i = 0; i < data_len; i++) {
                    data [i] ^= pinfo_.mask[(pinfo_.index + i) & 0x03];
                }
            }

            if ((data_len + pinfo_.index) < pinfo_.len) {
                // 帧未处理完成
                if (pinfo_.opcode == 0) { // 继续帧
                    pinfo_.num++;
                } else {                    // 首帧
                    pinfo_.num = 0;
                    pinfo_.message_opcode = pinfo_.opcode;
                }
                socket_->handleEvent(this, WS_EVT_DATA, &pinfo_, data, data_len);   //.........有不完整的数据到达了
                pinfo_.index += data_len;
                pstate_ = WS_PARSE_PAYLOAD;
            } else if ((data_len + pinfo_.index) == pinfo_.len) {
                // 帧处理完成
                switch (pinfo_.opcode) {
                  case WS_DISCONNECT:
                    if (data_len) {
                        uint16_t reason_code = (data[0] << 8) | data[1];
                        char* reason = (char*)(data + 2);
                        if (reason_code > 1001) {
                            // 大于1001为异常的关闭码
                            socket_->handleEvent(this, WS_EVT_ERROR, &reason_code, (uint8_t*)reason, strlen(reason));
                        }
                    }
                    if (status_ == WS_DISCONNECTING) {
                        status_ = WS_DISCONNECTED;
                        client_->close();
                    } else {
                        status_ = WS_DISCONNECTING;
                        client_->set_defer_ack(false);
                        queueControl(new AsyncWebSocketControl(WS_DISCONNECT, data, data_len));
                    }
                    break;
                  case WS_PING:
                    // 收到PING帧后自动发送PONG帧
                    queueControl(new AsyncWebSocketControl(WS_PONG, data, data_len));
                    break;
                  case WS_PONG:
                    // PONG帧载荷的长度/内容不同，触发PONG帧事件
                    if (data_len != sizeof(AWSC_PING_PAYLOAD) || memcmp(AWSC_PING_PAYLOAD, data, sizeof(AWSC_PING_PAYLOAD)) != 0) {
                        socket_->handleEvent(this, WS_EVT_PONG, nullptr, data, data_len);
                    }
                    break;
                  default:
                    socket_->handleEvent(this, WS_EVT_DATA, &pinfo_, data, data_len);
                }
                pstate_ = WS_PARSE_HEAD;
            } else {
                // 解析出错了，怎么可以会比预期的数据还多
                ESP_LOGE(TAG, "WebSocket Frame slove failed, close it.");
                close();
            }
            data += data_len;
            buf_len -= data_len;
        }
    }
}

inline void AsyncWebSocketClient::onPoll()
{
    if (client_->get_send_buffer_size() && ((!controlQueue_.isEmpty())||(!messageQueue_.isEmpty()))) {
        runQueue();
    } else if (keepAlivePeriod_ && controlQueue_.isEmpty() && messageQueue_.isEmpty() 
        && SystemInfo::Timeout(lastMessageTime_, keepAlivePeriod_)) {
            ping((uint8_t*)AWSC_PING_PAYLOAD, sizeof(AWSC_PING_PAYLOAD));
    }
}

/// @brief 发送关闭帧断开连接
/// @param code 
/// @param message 
void AsyncWebSocketClient::close(uint16_t code, const char* message)
{
    if (status_ != WS_CONNECTED) {
        return;
    }
    if (code) {
        uint8_t packet_len = 2;
        if (message != nullptr) {
            size_t message_len = strlen(message);
            packet_len = std::min((size_t)125, 2 + message_len);
        }
        auto* buf = new char[packet_len];
        if (buf != nullptr) {
            buf[0] = (uint8_t)(code >> 8);
            buf[1] = (uint8_t)(code & 0xff);
            if (message != nullptr) {
                memcpy(buf + 2, message, packet_len - 2);
            }
            queueControl(new AsyncWebSocketControl(WS_DISCONNECT, (uint8_t*)buf, packet_len));
            return;
        }
    }
    queueControl(new AsyncWebSocketControl(WS_DISCONNECT));
}

/// @brief 向控制帧队列中添加一个PING帧
/// @param data 可选的数据
/// @param len 数据的长度
void AsyncWebSocketClient::ping(uint8_t* data, size_t len)
{
    if (status_ == WS_CONNECTED) {
        queueControl(new AsyncWebSocketControl(WS_PING, data, len));
    }
}

/// @brief 检查消息队列是否已満
inline bool AsyncWebSocketClient::queueIsFull()
{
    return ((messageQueue_.length() >= CONFIG_WS_MAX_QUEUE_MESSAGES) || (status_ != WS_CONNECTED));
}

/// @brief 添加一个消息帧并尝试发送
void AsyncWebSocketClient::queueMessage(AsyncWebSocketMessage* message)
{
    if (message == nullptr) {
        return;
    }

    if (status_ != WS_CONNECTED) {
        delete message;
        return;
    }

    if (messageQueue_.length() >= CONFIG_WS_MAX_QUEUE_MESSAGES) {
        delete message;
        ESP_LOGW(TAG, "Message reject, queue is full.");
    } else {
        messageQueue_.add(message);
    }

    if (client_->get_send_buffer_size()) {
        runQueue();
    }
}

/// @brief 添加一个控制帧并尝试发送
void AsyncWebSocketClient::queueControl(AsyncWebSocketControl* control)
{
    if (control == nullptr) {
        return;
    }

    controlQueue_.add(control);
    if (client_->get_send_buffer_size()) {
        runQueue();
    }
}

/// @brief 尝试发送合适的websocket帧
void AsyncWebSocketClient::runQueue()
{
    while (!messageQueue_.isEmpty() && messageQueue_.front()->finished()) {
        messageQueue_.remove(messageQueue_.front());
    }

    if (!controlQueue_.isEmpty()                                                            // 存在控制帧
    && (messageQueue_.isEmpty() || messageQueue_.front()->betweenFrames())                  // 不存在消息帧、消息帧处于帧间态（不在发送中）
    && (client_->get_send_buffer_size() >= controlQueue_.front()->len() + 6)) {             // 为简化发送，只在可一次性发送控制帧时才发送(payload+2+4)
    // && webSocketSendFrameWindow(client_) > (size_t)(controlQueue_.front()->len() - 1)) {    // 可以发送整个控制帧（控制帧不能分片！）
        controlQueue_.front()->send(client_);
    } else if (!messageQueue_.isEmpty()                 // 存在消息帧
            && messageQueue_.front()->betweenFrames()   // 消息帧处于帧间状态
            && client_->get_send_buffer_size()) {       // 有发送空间
        messageQueue_.front()->send(client_);
    }
}

/// @brief 获取对端IP
ip_addr_t AsyncWebSocketClient::remoteIP()
{
    if (client_ == nullptr) {
        return IPADDR4_INIT(0);
    }
    return client_->get_remote_IP();
}

/// @brief 获取对端Port
uint16_t AsyncWebSocketClient::remotePort()
{
    return client_ == nullptr ? 0 : client_->get_remote_port();
}








