#ifndef ASYNCWEBSOCKETMULTIMESSAGE_H_
#define ASYNCWEBSOCKETMULTIMESSAGE_H_

#include "AsyncWebSocketMessage.h"

class AsyncWebSocketMessageBuffer;

/// @brief 用于广播消息
///
/// 【异步发送模型】
///   - 每帧一个 SendPool chunk（通过 Append 入队）
///   - 帧缓冲 malloc + chunk cb free
///   - 帧完成通知：chunk cb 里调用 onFrameSent(frame_len)
///   - waiting_ 保证帧串行（一帧未 ACK 前不发下一帧）
class AsyncWebSocketMultiMessage : public AsyncWebSocketMessage {
public:
    AsyncWebSocketMultiMessage(AsyncWebSocketMessageBuffer* buffer,
                               uint8_t opcode = WS_TEXT, bool mask = false);
    virtual ~AsyncWebSocketMultiMessage() override;

    virtual bool betweenFrames() const override { return !waiting_; }
    virtual void ack(size_t len, uint32_t time) override;
    virtual size_t send(AsyncConnection* client) override;

    /// @brief chunk cb 触发时调用（在后台线程）
    void onFrameSent(size_t frame_len);

private:
    size_t                          len_;       // 消息总长度
    size_t                          sent_;      // 已发送的载荷字节数
    size_t                          ack_;       // 需要 ACK 的总帧字节数（含帧头）
    size_t                          acked_;     // 已收到 ACK 的帧字节数
    uint8_t*                        data_;      // 消息数据指针
    AsyncWebSocketMessageBuffer*    WSbuffer_;  // 共享缓冲引用
    bool                            waiting_;   // 是否在等当前帧 ACK
};

#endif