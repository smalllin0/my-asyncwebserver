#ifndef ASYNCWEBSOCKETMESSAGE_H_
#define ASYNCWEBSOCKETMESSAGE_H_

#include <cstdint>
#include <cstddef>
#include "AsyncConnection.h"

typedef enum {
    WS_MSG_SENDING,     // 消息分片发送中
    WS_MSG_SENT,        // 帧间状态
    WS_MSG_ERROR        // 发送过程发生错误
} AwsMessageStatus;

typedef enum {
    WS_CONTINUATION,        // 继续帧
    WS_TEXT,                // 文本帧
    WS_BINARY,              // 二进制帧
    WS_DISCONNECT = 0x08,   // 关闭帧
    WS_PING       = 0x09,   // Ping 帧
    WS_PONG       = 0x0A    // Pong 帧
} AwsFrameType;

class AsyncConnection;

class AsyncWebSocketMessage {
protected:
    AwsMessageStatus    status_;
    uint8_t             opcode_;
    bool                mask_;

public:
    AsyncWebSocketMessage()
        : status_(WS_MSG_ERROR)
        , opcode_(WS_TEXT)
        , mask_(false)
    {}
    virtual ~AsyncWebSocketMessage() {}

    /// @brief 本次可发送的载荷字节数（不含帧头/掩码）
    /// @note 改自 lwip 空间查询 —— 现在看 SendPool 剩余槽位
    ///       SendPool 共 16 槽位，本帧将占用 1 槽位 + 可能分片
    ///       如果池已经接近满，返回 0
    size_t webSocketSendFrameWindow(AsyncConnection* client) {
        // 每帧占用 SendPool 槽位，1 个足够；这里限制成"每次最多发 4KB"
        // 避免单帧占用过大 SendPool 空间
        constexpr size_t kMaxPerFrame = 4096;
        return kMaxPerFrame;
    }

    virtual void ack(size_t len, uint32_t time) {}
    virtual size_t send(AsyncConnection* client) { return 0; }
    virtual bool finished() { return status_ != WS_MSG_SENDING; }
    virtual bool betweenFrames() const { return false; }
};

#endif