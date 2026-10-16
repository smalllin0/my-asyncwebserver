#include "AsyncWebSocketBasicMessage.h"
#include "esp_log.h"
#include <cstring>

#define TAG "AsyncWebSocketBasicMessage"

AsyncWebSocketBasicMessage::AsyncWebSocketBasicMessage(const char* data, size_t len,
                                                       uint8_t opcode, bool mask)
    : len_(len), sent_(0), ack_(0), acked_(0), waiting_(false)
{
    opcode_ = opcode & 0x07;
    mask_   = mask;
    data_ = new (std::nothrow) uint8_t[len + 1];
    if (data_ == nullptr) {
        len_ = 0;
        status_ = WS_MSG_ERROR;
    } else {
        status_ = WS_MSG_SENDING;
        memcpy(data_, data, len);
        data_[len] = 0;
    }
}

AsyncWebSocketBasicMessage::AsyncWebSocketBasicMessage(uint8_t opcode, bool mask)
    : len_(0), sent_(0), ack_(0), acked_(0), data_(nullptr), waiting_(false)
{
    opcode_ = opcode & 0x07;
    mask_   = mask;
}

AsyncWebSocketBasicMessage::~AsyncWebSocketBasicMessage()
{
    if (data_ != nullptr) {
        delete[] data_;
    }
}

void AsyncWebSocketBasicMessage::ack(size_t len, uint32_t time)
{
    acked_ += len;
    if (sent_ == len_ && acked_ >= ack_) {
        status_ = WS_MSG_SENT;
    }
}

/// @brief 帧已被 ACK（由 chunk cb 调用）
void AsyncWebSocketBasicMessage::onFrameSent(size_t frame_len)
{
    ack(frame_len, 0);
}

/// @brief 尝试发送消息
/// @return 本次入队的载荷字节数（不含帧头）
size_t AsyncWebSocketBasicMessage::send(AsyncConnection* client)
{
    if (status_ != WS_MSG_SENDING) return 0;
    if (waiting_) return 0;                        // 等上一帧 ACK
    if (sent_ >= len_) {
        if (acked_ >= ack_) status_ = WS_MSG_SENT;
        return 0;
    }

    size_t to_send = len_ - sent_;
    size_t window = webSocketSendFrameWindow(client);
    if (to_send > window) to_send = window;

    bool final = (sent_ + to_send == len_);
    uint8_t opcode = (sent_ == 0) ? opcode_ : static_cast<uint8_t>(WS_CONTINUATION);

    // 构造整个帧（含帧头 + 掩码）
    size_t frame_len = 0;
    auto* frame = webSocketMakeFrame(final, opcode, mask_,
                                     data_ + sent_, to_send, &frame_len);
    if (!frame) return 0;

    // 构造 cb 上下文，负责 free 帧缓冲并通知本消息
    struct Ctx {
        AsyncWebSocketBasicMessage* msg;
        uint8_t*                    buf;
        size_t                      frame_len;
    };
    auto* ctx = new (std::nothrow) Ctx{this, frame, frame_len};
    if (!ctx) { free(frame); return 0; }

    bool ok = client->Append(frame, frame_len, /*flags=*/0,   // no-copy，SendPool 已持有指针
        [](void* c, size_t /*acked*/) {
            auto* cx = static_cast<Ctx*>(c);
            cx->msg->onFrameSent(cx->frame_len);              // 通知本消息
            free(cx->buf);                                    // 释放帧缓冲
            delete cx;
        },
        ctx);

    if (!ok) {
        free(frame);
        delete ctx;
        return 0;
    }

    sent_ += to_send;
    ack_  += frame_len;                                        // 计入帧头长度
    waiting_ = true;
    return to_send;
}