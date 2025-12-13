#ifndef ASYNCWEBSOCKETMESSAGEBUFFER_H_
#define ASYNCWEBSOCKETMESSAGEBUFFER_H_

#include "sys/_stdint.h"
#include "stddef.h"


/// 广播消息存储类
class AsyncWebSocketMessageBuffer {
private:
    uint8_t*    data_;  // 原始数据存储位置
    size_t      len_;   // 数据长度
    uint16_t    count_; // 当前Buffer被引用次数
    bool        lock_;  // 操作锁（广播时防止被提前释放）

public:
    AsyncWebSocketMessageBuffer();
    AsyncWebSocketMessageBuffer(size_t size);
    AsyncWebSocketMessageBuffer(uint8_t* data, size_t size);
    AsyncWebSocketMessageBuffer(AsyncWebSocketMessageBuffer &&);
    ~AsyncWebSocketMessageBuffer();

    void operator++(int i) { count_++; }
    void operator--(int i) { if(count_ > 0) count_--; }
    void lock() { lock_ = true; }
    void unlock() { lock_ = false; }
    uint8_t* get() { return data_; }
    size_t length() { return len_; }
    uint16_t count() { return count_; }
    bool canDelete() { return !count_ && !lock_; }
    /// @brief 改变容量大小（不保存数据）
    bool reserve(size_t size);
};


#endif