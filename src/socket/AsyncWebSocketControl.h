#ifndef ASYNCWEBSOCKETCONTROL_H_
#define ASYNCWEBSOCKETCONTROL_H_

#include "AsyncSendFrame.h"

/// @brief 控制帧
class AsyncWebSocketControl {
private:
    bool    mask_;      // 是否启用掩码
    bool    finished_;  // 帧是否发送完成
    uint8_t opcode_;    // 帧操作码
    uint8_t *data_;     // 有效栽荷数据指针
    size_t  len_;       // 有效载荷长度

public:

    // 构造一个控制帧（需要数据拷贝，可优化）
    AsyncWebSocketControl (uint8_t opcode, uint8_t* data=nullptr, size_t len=0, bool mask=false) 
        : mask_(len && mask)
        , finished_(false)
        , opcode_(opcode)
        , len_(data ? (len > 125 ? 125 : len) : 0)  // 协议规范控制帧最长不超125
    {
        if (len_ > 0) {
            data_ = new uint8_t[len_];
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
    size_t len() {return len_ + 2; }
    size_t send(AsyncClient* c) {
        finished_ = true;
        return webSocketSendFrame(c, true, opcode_ & 0x0f, mask_, data_, len_);
    }
    char* data() {return (char*)data_; }
};

#endif