#ifndef ASYNCWEBSERVERRESPONSE_H_
#define ASYNCWEBSERVERRESPONSE_H_

#include <string>
#include "../StringArray.h"

#define CONFIG_TEMPLATE_PLACEHOLDER     '%'
#define CONFIG_TEMPLATE_PARAM_NAME_LENGTH   32


extern const char * WS_STR_CONNECTION;
extern const char * WS_STR_UPGRADE;
extern const char * WS_STR_ORIGIN;
extern const char * WS_STR_VERSION;
extern const char * WS_STR_KEY;
extern const char * WS_STR_PROTOCOL;
extern const char * WS_STR_ACCEPT;
extern const char * WS_STR_UUID;


class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncWebHeader;
class AsyncWebParameter;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncStaticWebHandler;
class AsyncCallbackWebHandler;
class AsyncResponseStream;

enum WebResponseState { // 响应的生命周期状态
    RESPONSE_SETUP,     // 初始化阶段
    RESPONSE_HEADERS,   // 发送头信息阶段
    RESPONSE_CONTENT,   // 发送内容阶段
    RESPONSE_WAIT_ACK,  // 等待客户端确认（ACK）
    RESPONSE_END,       // 响应结束
    RESPONSE_FAILED     // 响应失败
};

using AwsResponseFiller = std::function<size_t(uint8_t* buffer, size_t maxLen, size_t index)>;
using AwsTemplateProcessor = std::function<std::string(const std::string &)>;


class AsyncWebServerResponse {
public:
    AsyncWebServerResponse(uint16_t code=0, const std::string& contentType=empty_string);
    virtual ~AsyncWebServerResponse();
    virtual void setCode(uint16_t code) {
        if (state_ == RESPONSE_SETUP) {
            code_ = code;
        }
    }
    virtual void setContentLength(size_t len) {
        if (state_ == RESPONSE_SETUP) {
            contentLength_ = len;
        }
    }
    virtual void setContentType(const std::string &type) {
        if (state_ == RESPONSE_SETUP) {
            contentType_ = type;
        }
    }
    virtual void addHeader(std::string name, std::string value);
    virtual std::string assembleHead(uint8_t version);
    virtual bool started() const {
        return state_ > RESPONSE_SETUP;
    }
    virtual bool finished() const {
        return state_ > RESPONSE_WAIT_ACK;
    }
    virtual bool failed() const {
        return state_ == RESPONSE_FAILED;
    }
    virtual bool sourceValid() const {
        return false;
    }
    virtual void respond(AsyncWebServerRequest* req);
    virtual size_t ack(AsyncWebServerRequest* req, size_t len, uint32_t time) {
        return 0;
    }
protected:
    const char* responseCodeToString(uint16_t code);

    bool    sendContentLength_;                 // 是否发送Content-Length头
    bool    chunked_;                           // 是否使用分块传输
    int16_t code_;                              // 响应状态码
    size_t  contentLength_;                     // 响应内容长度（为0表示未知）
    size_t  headLength_;                        // 已发送的头信息长度
    size_t  sentLength_;                        // 响应体已发送的长度
    size_t  ackedLength_;                       // 客户端确认接收字节数（用于流控）
    size_t  writtenLength_;                     // 添加到客户端的数据长度
    WebResponseState    state_;                 // 当前响应所处的状态
    std::string         contentType_;           // 内容类型
    LinkedList<AsyncWebHeader*>     headers_;   // 所有的响应头
};

#endif // !ASYNCWEBSERVERRESPONSE_H_