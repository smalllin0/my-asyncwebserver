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
class AsyncConnection;

enum WebResponseState {
    RESPONSE_SETUP,
    RESPONSE_HEADERS,
    RESPONSE_CONTENT,
    RESPONSE_WAIT_ACK,
    RESPONSE_END,
    RESPONSE_FAILED
};

using AwsResponseFiller = std::function<size_t(uint8_t* buffer, size_t maxLen, size_t index)>;
using AwsTemplateProcessor = std::function<std::string(const std::string &)>;

class AsyncWebServerResponse {
public:
    AsyncWebServerResponse(uint16_t code = 0, const std::string& contentType = empty_string);
    virtual ~AsyncWebServerResponse();

    virtual void setCode(uint16_t code) {
        if (state_ == RESPONSE_SETUP) code_ = code;
    }
    virtual void setContentLength(size_t len) {
        if (state_ == RESPONSE_SETUP) contentLength_ = len;
    }
    virtual void setContentType(const std::string &type) {
        if (state_ == RESPONSE_SETUP) contentType_ = type;
    }
    virtual void addHeader(std::string name, std::string value);
    virtual std::string assembleHead(uint8_t version);

    virtual bool started()  const { return state_ > RESPONSE_SETUP; }
    virtual bool finished() const { return state_ > RESPONSE_WAIT_ACK; }
    virtual bool failed()   const { return state_ == RESPONSE_FAILED; }
    virtual bool sourceValid() const { return false; }

    virtual void respond(AsyncWebServerRequest* req);

    /// @brief 保留兼容老代码的空实现，新模型下不再被调用
    virtual size_t ack(AsyncWebServerRequest* req, size_t len, uint32_t time) {
        return 0;
    }

    /// @brief response 完成，通知 request 删除自己
    /// @note 只在 OnDrain 里调用（此时 SendPool 已空）
    void finish();

protected:
    const char* responseCodeToString(uint16_t code);

    bool    sendContentLength_;
    bool    chunked_;
    int16_t code_;
    size_t  contentLength_;
    size_t  sentLength_;
    WebResponseState state_;
    std::string contentType_;
    LinkedList<AsyncWebHeader*> headers_;

    // ★ 基类统一持有的上下文
    AsyncWebServerRequest* req_{nullptr};
    AsyncConnection*       client_{nullptr};
    std::string            header_;
};

#endif