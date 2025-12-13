#include "AsyncWebSocketBasicMessage.h"
#include "esp_log.h"

#define TAG "AsyncWebSocketBasicMessage"

/// @brief 基本消息构造函数
/// @param data 数据指针
/// @param len 数据长度
/// @param opcode 帧类型
/// @param mask 是否掩码
AsyncWebSocketBasicMessage::AsyncWebSocketBasicMessage(const char * data, size_t len, uint8_t opcode, bool mask)
    : len_(len)
    , sent_(0)
    , ack_(0)
    , acked_(0)
{
    opcode_ = opcode & 0x07;
    mask_ = mask;
    data_ = new uint8_t[len + 1];
    if (data_ == nullptr) {
        len_ = 0;
        status_ = WS_MSG_ERROR;
    } else {
        status_ = WS_MSG_SENDING;
        memcpy(data_, data, len);
        data_[len] = 0;
    }
}

/// @brief 基本消息构造函数
/// @param opcode 帧类型
/// @param mask 是否掩码
AsyncWebSocketBasicMessage::AsyncWebSocketBasicMessage(uint8_t opcode, bool mask)
    : len_(0)
    , sent_(0)
    , ack_(0)
    , acked_(0)
    , data_(nullptr)
{
    opcode_ = opcode & 0x07;
    mask_ = mask;
}

AsyncWebSocketBasicMessage::~AsyncWebSocketBasicMessage()
{
    if (data_ != nullptr) {
        delete[] data_;
    }
}

/// @brief 记录收到确认的字节数
/// @param len
/// @param time
void AsyncWebSocketBasicMessage::ack(size_t len, uint32_t time)
{
    acked_ += len;
    if (sent_ == len_ && acked_ == ack_) {
        status_ = WS_MSG_SENT;
    }
}

/// @brief 发送WebSocket消息
/// @param client
/// @return 发送的字节数
size_t AsyncWebSocketBasicMessage::send(AsyncClient* client)
{
    if (status_ != WS_MSG_SENDING) {
        return 0;
    }
    if (acked_ < ack_) {  // 流控
        return 0;
    }
    if (sent_ == len_) {
        if (acked_ == ack_) {
            status_ = WS_MSG_SENT;
        }
        return 0;
    }
    if (sent_ > len_) {
        status_ = WS_MSG_ERROR;
        return 0;
    }

    size_t toSend = len_ - sent_;
    size_t window = webSocketSendFrameWindow(client);

    if (window < toSend) {
        toSend = window;
    } else {
        //...........这里可以使用不复制发送
    }

    sent_ += toSend;
    ack_ += toSend + ((toSend < 126) ? 2 : 4) + (mask_ * 4);

    bool final = (sent_ == len_);
    uint8_t* dPtr = (uint8_t*)(data_ + (sent_ - toSend));
    uint8_t opCode = (toSend && sent_ == toSend) ? opcode_ : (uint8_t)WS_CONTINUATION;
    
    
    size_t sent = webSocketSendFrame(client, final, opCode, mask_, dPtr, toSend);
    status_ = WS_MSG_SENDING;
    if (toSend && sent != toSend) {
        sent_ -= (toSend - sent);
        ack_ -= (toSend - sent);
    }

    return sent;
}