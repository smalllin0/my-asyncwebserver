#ifndef ASYNCWEBSOCKETMESSAGE_H_
#define ASYNCWEBSOCKETMESSAGE_H_

#include "sys/_stdint.h"
#include "stddef.h"
#include "AsyncClient.h"

typedef enum {
    WS_MSG_SENDING,     // 消息分片发送中
    WS_MSG_SENT,        // 帧间状态
    WS_MSG_ERROR        // 发送过程发生错误
} AwsMessageStatus;
typedef enum {
    WS_CONTINUATION,        // 继续帧
    WS_TEXT,                // 文本帧
    WS_BINARY,              // 二进制帧
    WS_DISCONNECT = 0x08,   // 关闭帧(RFC 6455规范)
    WS_PING = 0x09,         // Ping帧(RFC 6455规范)
    WS_PONG = 0x0A          // Pong帧(RFC 6455规范)
} AwsFrameType;

class AsyncClient;

class AsyncWebSocketMessage {
protected:
    AwsMessageStatus    status_;            // 消息发送状态
    uint8_t             opcode_;            // 帧操作码
    bool                mask_;              // 消息是否要掩码处理

public:
    AsyncWebSocketMessage()
        : status_(WS_MSG_ERROR)
        , opcode_(WS_TEXT)
        , mask_(false)
    {}
    virtual ~AsyncWebSocketMessage(){}
    size_t webSocketSendFrameWindow(AsyncClient* client) {
        size_t space = client->get_send_buffer_size();
        return space < 9 ? 0 : space - 8;
    }
    virtual void ack(size_t len, uint32_t time) {}
    virtual size_t send(AsyncClient* client) { return 0; }
    virtual bool finished() { return status_ != WS_MSG_SENDING; }
    virtual bool betweenFrames() const { return false; }

};

#endif