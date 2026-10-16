#ifndef ASYNCWEBSOCKETBASICMESSAGE_H_
#define ASYNCWEBSOCKETBASICMESSAGE_H_

#include "AsyncWebSocketMessage.h"
#include "AsyncConnection.h"
#include "AsyncSendFrame.h"

/// @brief 普通文本/二进制消息
class AsyncWebSocketBasicMessage : public AsyncWebSocketMessage {
private:
    size_t      len_;       // 消息总长度
    size_t      sent_;      // 已发送的字节数（不含帧头）
    size_t      ack_;       // 需要 ACK 的总字节数（含帧头）
    size_t      acked_;     // 已收到 ACK 的字节数
    uint8_t*    data_;      // 消息数据指针
    bool        waiting_;   // 是否在等当前帧 ACK

public:
    AsyncWebSocketBasicMessage(const char* data, size_t len,
                               uint8_t opcode = WS_TEXT, bool mask = false);
    AsyncWebSocketBasicMessage(uint8_t opcode = WS_TEXT, bool mask = false);
    virtual ~AsyncWebSocketBasicMessage() override;

    virtual bool betweenFrames() const override { return !waiting_; }
    virtual void ack(size_t len, uint32_t time) override;
    virtual size_t send(AsyncConnection* client) override;

    /// @brief chunk cb 触发时调用（在后台线程）
    void onFrameSent(size_t frame_len);
};

#endif