#include "AsyncWebSocketMultiMessage.h"
#include "AsyncWebSocketMessageBuffer.h"
#include <algorithm>
#include "AsyncSendFrame.h"

AsyncWebSocketMultiMessage::AsyncWebSocketMultiMessage(AsyncWebSocketMessageBuffer* buffer, uint8_t opcode, bool mask)
    : len_(0)
    , sent_(0)
    , ack_(0)
    , acked_(0)
    , WSbuffer_(buffer)
{
    opcode_ = opcode & 0x07;
    mask_ = mask;
    if (buffer) {
        (*WSbuffer_)++;  // 引用+1
        data_ = buffer->get();
        len_ = buffer->length();
        status_ = WS_MSG_SENDING;
    } else {
        status_ = WS_MSG_ERROR;
    }
}

AsyncWebSocketMultiMessage::~AsyncWebSocketMultiMessage()
{
    if (WSbuffer_) { (*WSbuffer_)-- ;}
}

void AsyncWebSocketMultiMessage::ack(size_t len, uint32_t time)
{
    acked_ += len;
    if (sent_ > len_ && acked_ >= ack_) {
        status_ = WS_MSG_SENT;
    }
}

size_t AsyncWebSocketMultiMessage::send(AsyncClient* client)
{
    if (status_ != WS_MSG_SENDING ) { return 0; }
    if (acked_ < ack_) { return 0; }    // 等待上一帧被接收才能发送
    if (sent_ == len_) {
        if (acked_ == ack_) { status_ = WS_MSG_SENT; }
        return 0;
    }
    if (sent_ > len_) {
        status_ = WS_MSG_ERROR;
        return 0;
    }

    auto to_send = std::min(len_ - sent_, webSocketSendFrameWindow(client));
    auto* data = data_ + sent_;
    sent_ += to_send; 
    // 数据长度小于126时消耗2字节帧头，否则需要4字节，启用掩码时额外需要4字节
    ack_ += to_send + ((to_send < 126) ? 2 : 4) + (mask_*4);
    uint8_t operate_code = (to_send && sent_ == to_send) ? opcode_ : (uint8_t)WS_CONTINUATION;
    
    auto sent = webSocketSendFrame(
        client,
        (sent_ == len_),
        operate_code,
        mask_,
        data,
        to_send
    );
    status_ = WS_MSG_SENDING;

    auto fail_byte = to_send - sent;
    if (to_send && fail_byte) {
        sent_ -= fail_byte;
        ack_ -= fail_byte;
    }

    return sent;
}