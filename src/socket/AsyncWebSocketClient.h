// =============================================================
// 文件：AsyncWebSocketClient.h
// =============================================================
#ifndef ASYNCWEBSOCKETCLIENT_H_
#define ASYNCWEBSOCKETCLIENT_H_

#include "../StringArray.h"
#include "AsyncWebSocketBasicMessage.h"
#include "AsyncWebSocketMultiMessage.h"

#define CONFIG_WS_MAX_QUEUE_MESSAGES    32
#define CONFIG_MAX_WS_CLIENTS           8

class AsyncConnection;
class AsyncWebSocket;
class AsyncWebServerRequest;
class AsyncWebSocketControl;
class AsyncWebSocketMessageBuffer;

typedef enum {
    WS_DISCONNECTED,    // 客户端未连接
    WS_CONNECTED,       // 客户端已成功连接
    WS_DISCONNECTING    // 客户端正在断开连接
} AwsClientStatus;

typedef enum {
    WS_PARSE_HEAD,      // 正在解析头部
    WS_PARSE_PAYLOAD    // 正在解析载荷
} WsParseState;

struct AwsFrameInfo {
    uint8_t     message_opcode;     // 消息操作码
    uint8_t     final;              // 最后一帧标志
    uint8_t     masked;             // 当前帧是否被掩码
    uint8_t     opcode;             // 当前帧操作码
    uint32_t    num;                // 当前帧在消息中的编号
    uint32_t    len;                // 当前帧有效载荷长度
    uint8_t     mask[4];            // 掩码密钥
    uint32_t    index;              // 当前帧载荷偏移
};

/// @brief 客户端对象，接管升级后的 websocket 连接
///
/// 【异步发送模型】
///   - 每帧一个 SendPool chunk（通过 Append 入队）
///   - 帧缓冲 malloc + chunk cb free
///   - 帧完成通知：chunk cb 里直接调用 msg->onFrameSent(frame_len)
///   - 队列推进由 runQueue() 驱动，由 queueMessage/queueControl/onPoll 触发
class AsyncWebSocketClient {
public:
    AsyncWebSocketClient(AsyncWebServerRequest* req, AsyncWebSocket* server);
    ~AsyncWebSocketClient();

    void closeClient() { if (conn_) conn_->Close(); }

    uint16_t id() { return id_; }
    AwsClientStatus status() { return status_; }
    AsyncConnection* client() { return conn_; }
    AsyncWebSocket* server() { return socket_; }
    AwsFrameInfo const &pinfo() const { return pinfo_; }
    uint16_t keepAlivePeriod() { return (uint16_t)(keepAlivePeriod_ / 1000); }
    void keepAlivePeriod(uint16_t seconds) { keepAlivePeriod_ = seconds * 1000; }
    void message(AsyncWebSocketMessage* msg) { queueMessage(msg); }
    bool canSend() { return messageQueue_.length() < CONFIG_WS_MAX_QUEUE_MESSAGES; }

    ip_addr_t remoteIP();
    uint16_t  remotePort();
    void Close(uint16_t code = 0, const char* message = nullptr);
    void ping(uint8_t* data = nullptr, size_t len = 0);
    bool queueIsFull();

    // ---------- 文本/二进制快捷接口 ----------
    void text(const char* message) {
        queueMessage(new AsyncWebSocketBasicMessage(message, strlen(message)));
    }
    void text(uint8_t* message, size_t len) {
        queueMessage(new AsyncWebSocketBasicMessage((char*)message, len));
    }
    void text(AsyncWebSocketMessageBuffer* buffer) {
        queueMessage(new AsyncWebSocketMultiMessage(buffer));
    }
    void binary(const char* message) {
        queueMessage(new AsyncWebSocketBasicMessage(message, strlen(message), WS_BINARY));
    }
    void binary(uint8_t* message, size_t len) {
        queueMessage(new AsyncWebSocketBasicMessage((char*)message, len, WS_BINARY));
    }
    void binary(AsyncWebSocketMessageBuffer* buffer) {
        queueMessage(new AsyncWebSocketMultiMessage(buffer, WS_BINARY));
    }

    // ---------- 内部回调（由 AsyncConnection 触发） ----------
    inline void onPoll();                          // 定期驱动队列 / PING
    inline void onFree();
    inline void onData(void* buf, size_t len);     // 帧解析

private:
    void queueMessage(AsyncWebSocketMessage* message);
    void queueControl(AsyncWebSocketControl* control);
    void runQueue();                                // 按优先级发送控制帧 / 消息帧

    AsyncConnection*    conn_;                  // 关联的连接
    AsyncWebSocket*     socket_;                // 关联的服务器
    uint16_t            id_;                    // 客户端 ID
    AwsClientStatus     status_;                // 客户端状态
    WsParseState        pstate_;                // 当前解析状态
    AwsFrameInfo        pinfo_;                 // 当前帧元信息
    uint32_t            lastMessageTime_;       // 最后通信时间戳
    uint32_t            keepAlivePeriod_;       // 心跳周期（毫秒）

    std::string         frameHeaderCache_;      // 缓存的帧头
    LinkedList<AsyncWebSocketControl*>  controlQueue_;
    LinkedList<AsyncWebSocketMessage*>  messageQueue_;
};

#endif