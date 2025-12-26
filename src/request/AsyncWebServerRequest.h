#ifndef ASYNCWEBSERVERREQUEST_H_
#define ASYNCWEBSERVERREQUEST_H_

#include <functional>
#include <string>
#include "../StringArray.h"
#include "lwip/err.h"
#include "../handler/AsyncStaticWebHandler.h"
#include "AsyncClient.h"



class AsyncClient;
class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncWebHeader;
class AsyncWebParameter;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncStaticWebHandler;
class AsyncCallbackWebHandler;
class AsyncWebSocket;
class AsyncWebSocketResponse;

// class AsyncResponseStream;


typedef enum {
    HTTP_GET     = 0b00000001,
    HTTP_POST    = 0b00000010,
    HTTP_DELETE  = 0b00000100,
    HTTP_PUT     = 0b00001000,
    HTTP_PATCH   = 0b00010000,
    HTTP_HEAD    = 0b00100000,
    HTTP_OPTIONS = 0b01000000,
    HTTP_ANY     = 0b01111111,
} WebRequestMethod;
#define RESPONSE_TRY_AGAIN  0xFFFFFFFF

using WebRequestMethodComposite = uint8_t;
// using ArDisconnectHandler = std::function<void(void)>;
using ArDisconnectHandler = void(*)(void);

/// @brief 标识客户端与服务器之间不同类型的连接或请求类型
enum RequestedConnectionType {
    RCT_NOT_USED = -1,  // 未使用的连接类型
    RCT_DEFAULT = 0,    // 默认连接类型，通常表示未明确指定协议的请求
    RCT_HTTP,           // 标准的 HTTP 连接（如 GET/POST 请求）
    RCT_WS,             // WebSocket 连接（用于实时双向通信）
    RCT_EVENT,          // 表示事件流（如 Server-Sent Events, SSE）或其他自定义事件协议
    RCT_MAX             // 枚举的最大值，循环遍历所有有效枚举值
};


using AwsResponseFiller = std::function<size_t(uint8_t* buffer, size_t maxLen, size_t index)>;



/// 封装和处理单个 HTTP 请求，涵盖了请求的完整生命周期和相关信息。
/// 它是客户端请求的抽象表示，负责解析请求内容、管理参数、头信息，并协调生成响应。
class AsyncWebServerRequest {
public:
    AsyncWebServerRequest();
    // AsyncWebServerRequest(AsyncWebServer* server, AsyncClient* client);


    void onDisconnect(ArDisconnectHandler fn) {
        onDisconnectfn_ = fn;
    }
    AsyncClient* client() {
        return client_;
    }
    uint8_t version() const {
        return version_;
    }
    WebRequestMethodComposite method() const {
        return method_;
    }
    const std::string& url() const {
        return url_;
    }
    const std::string& host() const {
        return host_;
    }
    const std::string& contentType() const {
        return contentType_;
    }
    size_t contentLength() const {
        return contentLength_;
    }
    bool multipart() const {
        return isMultipart_;
    }
    RequestedConnectionType requestedConnType() const {
        return reqconntype_;
    }
    void setHandler(AsyncWebHandler* handler) {
        handler_ = handler;
    }
    /// @brief 获取请求头的个数
    size_t headers() const
    {
        return headers_.length();
    }
    /// @brief 获取请求中的参数个数
    size_t params() const {
        return params_.length();
    }
    size_t args() const {
        return params();
    }
    
    const char* methodToString() const;
    const char* requestedConnTypeToString() const;
    bool isExpectedRequestedConnType(RequestedConnectionType erct1, RequestedConnectionType erct2=RCT_NOT_USED, RequestedConnectionType erct3=RCT_NOT_USED);
    
    bool hasHeader(const std::string &name) const;
    bool hasParam(const std::string &name, bool post = false, bool file = false) const;
    bool hasArg(const char* name) const;
    AsyncWebHeader* getHeader(const std::string &name) const;
    AsyncWebHeader* getHeader(size_t index) const;     
    AsyncWebParameter* getParam(const std::string &name, bool post = false, bool file = false) const;
    AsyncWebParameter* getParam(size_t index) const;
    const std::string& arg(const std::string& name) const;
    const std::string& arg(size_t index) const;
    const std::string& argName(size_t index) const;
    const std::string& header(const char* name) const;
    const std::string& header(size_t index) const;
    const std::string& headerName(size_t index) const;
    
    bool authenticate(const char* hash);
    bool authenticate(const char* username, const char* passwd, const char* realm=nullptr, bool passwdIsHash=false);
    void requestAuthentication(const char* realm=nullptr, bool isDigest=true);
    
    void addInterestingHeader(std::string name);
    void redirect(std::string url);

    // const std::string& ASYNCWEBSERVER_REGEX_ATTRIBUTE pathArg(size_t i) const;

    void send(AsyncWebServerResponse* response);
    /// @brief 发送一个基本的响应
    /// @param code HTTP状态码
    /// @param contentType 内容类型
    /// @param content 内容
    void send(int code, std::string contentType="", std::string content="") {
        send(beginResponse(code, std::move(contentType), std::move(content)));
    }
    void send(std::string path, std::string contentType="", bool download=false, AwsTemplateProcessor callback=nullptr);
    void sendChunked(std::string contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback=nullptr);
    void send_P(int code, std::string contentType, const uint8_t* content, size_t len, AwsTemplateProcessor callback=nullptr);

    AsyncWebServerResponse* beginResponse(int code, std::string contentType ="", std::string content="");
    AsyncWebServerResponse* beginResponse(std::string path, std::string contentType ="", bool download = false, AwsTemplateProcessor callback = nullptr);
    AsyncWebServerResponse* beginResponse(std::string contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
    AsyncWebServerResponse* beginChunkedResponse(std::string contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
    AsyncWebServerResponse* beginResponse_P(int code, std::string contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr);
    // 暂不实现流式响应，通过chunked间接可完成
    // AsyncResponseStream *beginResponseStream(const std::string &contentType, size_t bufferSize = 1460);


private:
    // 核心友元类声明
    friend class AsyncWebServer;           // 服务器管理
    friend class AsyncWebHandler;          // 处理器基类  
    friend class AsyncCallbackWebHandler;  // 回调处理器
    friend class AsyncStaticWebHandler;    // 静态文件处理器
    friend class AsyncWebServerResponse;   // 响应基类
    friend class AsyncBasicResponse;       // 基本响应
    friend class AsyncAbstractResponse;    // 抽象响应
    friend class AsyncWebRewrite;          // URL重写
    friend class DefaultHeaders;           // 默认头部
    friend class AsyncWebSocketResponse;
    friend class AsyncWebSocketClient;
    friend class AsyncWebSocket;

    ~AsyncWebServerRequest() {
        reset();
    }

    void init(AsyncWebServer* server, AsyncClient* client);
    void reset();
    inline void onPoll();
    inline void onAck(size_t len, uint32_t time);
    inline void onErr(err_t error);
    // inline void onTimeout(uint32_t time) {
    //     client_->close();
    // }
    inline void onData(void* buf, size_t len);
    void onDisconnect();

    void addParam(AsyncWebParameter* p) {
        params_.add(p);
    }
    void addPathParam(const char* param) {
        pathParams_.add(new std::string(param));
    }

    void parseReqLine(char* start, char* end);
    bool parseReqHeader(const char* start, const char* end);
    void parseLine(char* start, char* end);
    void parsePlainPost(uint8_t* data, size_t len);
    void parseMultiPartLine(uint8_t* start, uint8_t* end);
    void handleMultipartBody(void* buf, size_t len);
    void addGetParams(const char* start, const char* end);
    std::string urlDecode(const char* start, const char* end) const;

    void handleUpload(uint8_t* data, size_t len, bool last);           

    void removeNotInterestingHeaders();


    char*                   fileName_{nullptr};   
    AsyncWebServerRequest*  next_;                      // 下一请求
    AsyncClient*            client_;                    // 关联的连接
    AsyncWebServer*         server_;                    // 关联的服务器
    AsyncWebHandler*        handler_{nullptr};          // 处理该请求的处理器
    AsyncWebServerResponse* response_{nullptr};         // 当前请求的响应对象
    StringArray             interestingHeaders_;        // 关注的请求头
    ArDisconnectHandler     onDisconnectfn_{nullptr};   // 

    LinkedList<AsyncWebHeader*>     headers_;       // 所有的请求头
    LinkedList<AsyncWebParameter*>  params_;        // 请求参数（包括请求参数、表单数据、文件）
    LinkedList<std::string*>        pathParams_;    // 

    std::string             tmp_{};
    bool                    isFragmented_{false};
    uint8_t                 parseState_;

    uint8_t                     version_{0};            // 当前请求采用的HTTP协议版本
    WebRequestMethodComposite   method_{HTTP_ANY};      // 请求的方法
    std::string                 url_{};                 // 请求的URL
    std::string                 host_{};                // 请求的HOST
    std::string                 contentType_{};         // 请求内容类型
    std::string                 boundary_{};            // boundary边界字符串
    std::string                 authorization_{};       // 请求中的认证字段？？？？
    RequestedConnectionType     reqconntype_{RCT_HTTP}; // 连接类型

    bool        isDigest_{false};               // 是否为Digest认证
    bool        isMultipart_{false};            // 是否为多部分表单标记
    bool        isPlainPost_{false};            // 是否为表单数据
    bool        expectingContinue_{false};      // 客户端是否期望继续（收到服务器100）
    size_t      contentLength_{0};              // 请求体的字节数（Post数据名文件）
    size_t      parsedLength_{0};               // 请求体中处理的字节计数
    size_t      boundaryMatchLen_{0};

    uint8_t     multiParseState_{0};        // 解析多表单数据时所处的状态
    uint8_t     boundaryPosition_{0};       // 当前边界字符匹配的位置
    bool        itemIsFile_{false};         // 当前部分是否为文件标识
    size_t      itemStartIndex_{0};         // 当前处理部分在原始数据中的起始位置
    size_t      itemSize_{0};               // 当前处理部分的大小
    size_t      itemBufferIndex_{0};        // 缓冲区已使用字节数
    uint8_t*    itemBuffer_{nullptr};       // 存储当前部分数据的缓冲区
    std::string itemName_{};                // 当前部分名称
    std::string itemFileName_{};            // 当前文件为文件时，存储该文件文件名
    std::string itemType_{};                // 当前部分的类型
    std::string itemValue_{};               // 当前部分的值（不适用于文件类型）
};


#endif