#include "AsyncWebSocketMultiMessage.h"
#include "AsyncWebSocketMessageBuffer.h"
#include "AsyncSendFrame.h"
#include <algorithm>
#include <cstdlib>

AsyncWebSocketMultiMessage::AsyncWebSocketMultiMessage(AsyncWebSocketMessageBuffer* buffer,
                                                       uint8_t opcode, bool mask)
    : len_(0), sent_(0), ack_(0), acked_(0), data_(nullptr), WSbuffer_(buffer), waiting_(false)
{
    opcode_ = opcode & 0x07;
    mask_   = mask;
    if (buffer) {
        (*WSbuffer_)++;               // 引用 +1
        data_ = buffer->get();
        len_  = buffer->length();
        status_ = WS_MSG_SENDING;
    } else {
        status_ = WS_MSG_ERROR;
    }
}

AsyncWebSocketMultiMessage::~AsyncWebSocketMultiMessage()
{
    if (WSbuffer_) { (*WSbuffer_)--; }
}

void AsyncWebSocketMultiMessage::ack(size_t len, uint32_t time)
{
    acked_ += len;
    if (sent_ >= len_ && acked_ >= ack_) {
        status_ = WS_MSG_SENT;
    }
}

void AsyncWebSocketMultiMessage::onFrameSent(size_t frame_len)
{
    ack(frame_len, 0);
}

size_t AsyncWebSocketMultiMessage::send(AsyncConnection* client)
{
    if (status_ != WS_MSG_SENDING) return 0;
    if (waiting_) return 0;
    if (sent_ >= len_) {
        if (acked_ >= ack_) status_ = WS_MSG_SENT;
        return 0;
    }

    size_t to_send = std::min(len_ - sent_, webSocketSendFrameWindow(client));

    bool final = (sent_ + to_send == len_);
    uint8_t opcode = (sent_ == 0) ? opcode_ : static_cast<uint8_t>(WS_CONTINUATION);

    size_t frame_len = 0;
    auto* frame = webSocketMakeFrame(final, opcode, mask_,
                                     data_ + sent_, to_send, &frame_len);
    if (!frame) return 0;

    struct Ctx {
        AsyncWebSocketMultiMessage* msg;
        uint8_t*                    buf;
        size_t                      frame_len;
    };
    auto* ctx = new (std::nothrow) Ctx{this, frame, frame_len};
    if (!ctx) { free(frame); return 0; }

    bool ok = client->Append(frame, frame_len, 0,
        [](void* c, size_t /*acked*/) {
            auto* cx = static_cast<Ctx*>(c);
            cx->msg->onFrameSent(cx->frame_len);
            free(cx->buf);
            delete cx;
        },
        ctx);

    if (!ok) {
        free(frame);
        delete ctx;
        return 0;
    }

    sent_ += to_send;
    ack_  += frame_len;
    waiting_ = true;
    return to_send;
}