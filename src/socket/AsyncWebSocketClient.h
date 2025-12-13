#ifndef ASYNCWEBSOCKETCLIENT_H_
#define ASYNCWEBSOCKETCLIENT_H_

#include "../StringArray.h"
#include "AsyncWebSocketBasicMessage.h"
#include "AsyncWebSocketMultiMessage.h"


#define CONFIG_WS_MAX_QUEUE_MESSAGES    32
#define CONFIG_MAX_WS_CLIENTS   8

class AsyncClient;
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
    uint8_t     message_opcode;     // 消息操作码，标识整个消息的类型（WS_TEXT/WS_BINARY）
    uint8_t     final;              // 消息的最后一帧标志（1：最后一帧）
    uint8_t     masked;             // 当前帧是否被掩码保护
    uint8_t     opcode;             // 当前帧操作码
    uint32_t    num;                // 消息分片时，当前帧在消息中的编号
    uint32_t    len;                // 当前帧的有效载荷长度
    uint8_t     mask[4];            // 帧被掩码保护时，掩码密钥
    uint32_t    index;              // 当前帧有效载荷数据量的偏移量
};

/// @brief 客户端对象，接管升级后的websocket连接类
class AsyncWebSocketClient {
public:
    AsyncWebSocketClient(AsyncWebServerRequest* req, AsyncWebSocket* server);
    ~AsyncWebSocketClient();
    void closeClient() {
        client_->close();
    }

    uint16_t id() { return id_; }
    AwsClientStatus status() { return status_; }
    AsyncClient* client() { return client_; }
    AsyncWebSocket* server() { return socket_; }
    AwsFrameInfo const &pinfo() const { return pinfo_; }
    uint16_t keepAlivePeriod() { return (uint16_t)(keepAlivePeriod_/1000); }
    void keepAlivePeriod(uint16_t seconds) { keepAlivePeriod_ = seconds * 1000; }
    void message(AsyncWebSocketMessage* msg) { queueMessage(msg); }
    bool canSend() { return messageQueue_.length() < CONFIG_WS_MAX_QUEUE_MESSAGES; }
    
    ip_addr_t remoteIP();
    uint16_t remotePort();
    void close(uint16_t code=0, const char* message=nullptr);
    void ping(uint8_t* data=nullptr, size_t len=0);
    bool queueIsFull();


    /// @brief 以字符串为内容添加一个文本帧
    void text(const char* message) {
        queueMessage(new AsyncWebSocketBasicMessage(message, strlen(message)));
    }
    /// @brief 以给定的数据内容添加一个文本帧
    void text(uint8_t* message, size_t len) {
        queueMessage(new AsyncWebSocketBasicMessage((char*)message, strlen((char*)message)));
    }
    /// @brief 以给定的缓冲区添加一个广播文本帧
    void text(AsyncWebSocketMessageBuffer* buffer) {
        queueMessage(new AsyncWebSocketMultiMessage(buffer));
    }
    /// @brief 以字符串为内容添加一个二进制帧
    void binary(const char* message) {
        queueMessage(new AsyncWebSocketBasicMessage(message, strlen(message), WS_BINARY));
    }
    /// @brief 以给定的数据内容添加一个二进制帧
    void binary(uint8_t* message, size_t len) {
        queueMessage(new AsyncWebSocketBasicMessage((char*)message, len, WS_BINARY));
    }
    /// @brief 以给定的缓冲区添加一个广播二进制帧
    void binary(AsyncWebSocketMessageBuffer* buffer) {
        queueMessage(new AsyncWebSocketMultiMessage(buffer, WS_BINARY));
    }


    inline void onAck(size_t len, uint32_t time);
    inline void onPoll();                          // 用于定期检查是否要发PING
    // inline void onTimeout(uint32_t time);
    inline void onFree();
    inline void onData(void* buf, size_t len);     // 用于解析帧

private:

    void queueMessage(AsyncWebSocketMessage* message);
    void queueControl(AsyncWebSocketControl* control);
    void runQueue();                        // 按优先级发送控制帧、消息帧

    AsyncClient*    client_;    // 当前客户端关联的连接
    AsyncWebSocket* socket_;    // 当前客户端关联的服务器
    uint16_t        id_;        // 客户端ID
    AwsClientStatus status_;    // 客户端状态
    WsParseState    pstate_;    // 处理当前帧时，所处的状态
    AwsFrameInfo    pinfo_;     // 正在解析的帧的元信息
    uint32_t        lastMessageTime_;   // 最后一次通信时间戳
    uint32_t        keepAlivePeriod_;   // 心跳时间间隔(秒)

    std::string     frameHeaderCache_{};                // 缓存的帧头，默认为空
    LinkedList<AsyncWebSocketControl*>  controlQueue_;  // 需发送控制帧队列
    LinkedList<AsyncWebSocketMessage*>  messageQueue_;  // 需发送消息帧队列
};

#endif