#ifndef ASYNCWEBSOCKETMULTIMESSAGE_H_
#define ASYNCWEBSOCKETMULTIMESSAGE_H_

#include "AsyncWebSocketMessage.h"

class AsyncWebSocketMessageBuffer;

/// @brief 用于广播消息
class AsyncWebSocketMultiMessage : public AsyncWebSocketMessage {
public:
    AsyncWebSocketMultiMessage(AsyncWebSocketMessageBuffer* buffer, uint8_t opcode=WS_TEXT, bool mask=false);
    virtual ~AsyncWebSocketMultiMessage() override;
    virtual bool betweenFrames() const override { return acked_ == ack_; }
    virtual void ack(size_t len, uint32_t time) override;
    virtual size_t send(AsyncClient* client) override;


private:
    size_t  len_;
    size_t  sent_;
    size_t  ack_;
    size_t  acked_;
    uint8_t *data_;
    AsyncWebSocketMessageBuffer*    WSbuffer_;
};

#endif