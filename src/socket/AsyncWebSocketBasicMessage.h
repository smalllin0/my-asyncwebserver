#ifndef ASYNCWEBSOCKETBASICMESSAGE_H_
#define ASYNCWEBSOCKETBASICMESSAGE_H_

#include "AsyncWebSocketMessage.h"
#include "AsyncClient.h"
#include "AsyncSendFrame.h"

/// @brief 普通文本/二进制消息
class AsyncWebSocketBasicMessage : public AsyncWebSocketMessage {
private:
    size_t      len_;   // 消息总长度
    size_t      sent_;  // 已发送的字节数（不含头部）
    size_t      ack_;   // 需要确认的总字节数
    size_t      acked_; // 已收到确认的字节数
    uint8_t*    data_;  // 消息数据指针

public:
    AsyncWebSocketBasicMessage(const char* data, size_t len, uint8_t opcode=WS_TEXT, bool mask=false);
    AsyncWebSocketBasicMessage(uint8_t opcode=WS_TEXT, bool mask=false);
    virtual bool betweenFrames() const override { return acked_ == ack_; }
    virtual ~AsyncWebSocketBasicMessage() override;
    virtual void ack(size_t len, uint32_t time) override;
    virtual size_t send(AsyncClient* client) override;
    
};


#endif