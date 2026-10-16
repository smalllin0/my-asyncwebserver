#ifndef ASYNCWEBSOCKETCONTROL_H_
#define ASYNCWEBSOCKETCONTROL_H_

#include "AsyncSendFrame.h"
#include "AsyncConnection.h"
#include <cstring>
#include <cstdlib>

/// @brief 控制帧
class AsyncWebSocketControl {
private:
    bool     mask_;
    bool     finished_;
    uint8_t  opcode_;
    uint8_t* data_;
    size_t   len_;

public:
    AsyncWebSocketControl(uint8_t opcode, uint8_t* data = nullptr,
                          size_t len = 0, bool mask = false)
        : mask_(len && mask)
        , finished_(false)
        , opcode_(opcode)
        , len_(data ? (len > 125 ? 125 : len) : 0)   // 控制帧 ≤ 125 字节
    {
        if (len_ > 0) {
            data_ = new (std::nothrow) uint8_t[len_];
            if (data_) {
                memcpy(data_, data, len_);
            } else {
                len_ = 0;
            }
        } else {
            data_ = nullptr;
        }
    }

    virtual ~AsyncWebSocketControl() { if (data_) delete[] data_; }

    virtual bool finished() const { return finished_; }
    uint8_t opcode() { return opcode_; }
    size_t  len()    { return len_ + 2; }
    char*   data()   { return reinterpret_cast<char*>(data_); }

    /// @brief 发送控制帧
    /// @return 入队的帧字节数（0 表示失败/池满）
    size_t send(AsyncConnection* client)
    {
        if (finished_) return 0;

        size_t frame_len = 0;
        auto* frame = webSocketMakeFrame(true, opcode_ & 0x0f, mask_,
                                         data_, len_, &frame_len);
        if (!frame) return 0;

        // 控制帧完成标记（不需要等 ACK）
        finished_ = true;

        struct Ctx { uint8_t* buf; };
        auto* ctx = new (std::nothrow) Ctx{frame};
        if (!ctx) { free(frame); finished_ = false; return 0; }

        bool ok = client->Append(frame, frame_len, 0,
            [](void* c, size_t /*acked*/) {
                auto* cx = static_cast<Ctx*>(c);
                free(cx->buf);
                delete cx;
            },
            ctx);

        if (!ok) {
            free(frame);
            delete ctx;
            finished_ = false;
            return 0;
        }
        return frame_len;
    }
};

#endif