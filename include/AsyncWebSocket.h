#ifndef ASYNCWEBSOCKET_H_
#define ASYNCWEBSOCKET_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"   
#include "freertos/task.h"  
#include <string> 
#include "../src/StringArray.h"  
#include "../src/handler/AsyncWebHandler.h"
#include "../src/socket/AsyncWebSocketClient.h"


#define CONFIG_WS_MAX_QUEUE_MESSAGES    32
#define CONFIG_MAX_WS_CLIENTS   8


class AsyncWebLock {
public:
    AsyncWebLock() {
        lock_ = xSemaphoreCreateBinary();
        lockedBy_ = nullptr;
        xSemaphoreGive(lock_);
    }
    ~AsyncWebLock() { vSemaphoreDelete(lock_); }
    bool lock() const {
        auto task = xTaskGetCurrentTaskHandle();
        if (lockedBy_ != task) {
            xSemaphoreTake(lock_, portMAX_DELAY);
            lockedBy_ = task;
            return true;
        }
        return false;
    }
    void unlock() const {
        lockedBy_ = nullptr;
        xSemaphoreGive(lock_);
    }
private:
    SemaphoreHandle_t       lock_;      // 锁
    mutable TaskHandle_t    lockedBy_;  // 持锁线程
};

class AsyncWebLockGuard {
public:
    AsyncWebLockGuard(const AsyncWebLock& lock) { lock_ = lock.lock() ? &lock : nullptr; }
    ~AsyncWebLockGuard() { if (lock_) lock_->unlock(); }
private:
    const AsyncWebLock* lock_;
};




class AsyncWebSocket;
class AsyncWebSocketMessage;
class AsyncWebSocketMessageBuffer;


typedef enum {
    WS_EVT_CONNECT,     // 客户端成功连接
    WS_EVT_DISCONNECT,  // 连接被断开
    WS_EVT_PONG,        // 收到服务器的Pong响应
    WS_EVT_ERROR,       // 发生错误
    WS_EVT_DATA         // 接收至数据（文本/二进制）
} AwsEventType;
using AwsEventHandler = std::function<void(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType, void* arg, uint8_t* data, size_t len)>;

/// @brief 管理socket客户端
class AsyncWebSocket : public AsyncWebHandler {
public:
    using AsyncWebSocketClientLinkedList = LinkedList<AsyncWebSocketClient*>;

    AsyncWebSocket(const std::string& uri);
    ~AsyncWebSocket();

    const char* uri() const { return uri_.c_str(); }
    void enable(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }
    bool hasClient(uint16_t id) { return client(id) != nullptr; }
    void onEvent(AwsEventHandler h) { eventHandler_ = h; }
    uint16_t getNextId() { return next_id_ ++; }

    bool availableForWriteAll();
    bool availableForWrite(uint16_t id);
    size_t count() const;
    AsyncWebSocketClient* client(uint16_t id);
    
    void close(uint16_t id, uint16_t code=0, const char* message=nullptr);
    void closeAll(uint16_t code=0, const char* message=nullptr);
    void cleanupClients(uint16_t maxClients=CONFIG_MAX_WS_CLIENTS);

    void ping(uint16_t id, uint8_t* data=nullptr, size_t len=0);
    void pingAll(uint8_t* data=nullptr, size_t len=0);

    void text(uint16_t id, const char* message);
    inline void text(uint16_t id, const std::string& message) {
        text(id, message.c_str());
    }
    void textAll(const char* message);
    void textAll(AsyncWebSocketMessageBuffer* buffer);

    void binary(uint16_t id, const char* message);
    void binary(uint16_t id, const std::string& message);
    void binaryAll(const char* message);
    void binaryAll(AsyncWebSocketMessageBuffer* buffer);

    void message(uint16_t id, AsyncWebSocketMessage* message);
    void messageAll(AsyncWebSocketMessage* message);

    void addClient(AsyncWebSocketClient* client);
    void cleanClient(AsyncWebSocketClient* client);
    /// @brief 调用用户自定义的socket事件处理程序
    /// @param client socket客户端对象指针
    /// @param type 事件类型
    /// @param arg 传递给用户事件处理程序的参数
    /// @param data 数据
    /// @param len 数据长度
    inline void handleEvent(AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (eventHandler_) eventHandler_(this, client, type, arg, data, len);
    }
    virtual bool canHandle(AsyncWebServerRequest* req) override final;
    virtual void handleRequest(AsyncWebServerRequest* req) override final;

    void cleanBuffers();
    AsyncWebSocketClientLinkedList getClients() const;


    AsyncWebSocketMessageBuffer* makeBuffer(size_t size = 0);
    AsyncWebSocketMessageBuffer* makeBuffer(uint8_t* data, size_t size);
    LinkedList<AsyncWebSocketMessageBuffer*> buffers_;   

private:

    bool            enabled_;
    uint16_t        next_id_;
    std::string     uri_;
    AwsEventHandler eventHandler_;
    AsyncWebLock    lock_;
    AsyncWebSocketClientLinkedList  clients_;
};

#endif // !ASYNCWEBSOCKET_H_