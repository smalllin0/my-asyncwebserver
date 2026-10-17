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

    virtual void setCode(uint16_t code) { if (state_ == RESPONSE_SETUP) code_ = code; }
    virtual void setContentLength(size_t len) { if (state_ == RESPONSE_SETUP) contentLength_ = len; }
    virtual void setContentType(const std::string &type) { if (state_ == RESPONSE_SETUP) contentType_ = type; }
    virtual void addHeader(std::string name, std::string value);
    virtual std::string assembleHead(uint8_t version);

    virtual bool started()  const { return state_ > RESPONSE_SETUP; }
    virtual bool finished() const { return state_ > RESPONSE_WAIT_ACK; }
    virtual bool failed()   const { return state_ == RESPONSE_FAILED; }
    virtual bool sourceValid() const { return false; }

    // 固定骨架，子类不能再 override
    virtual void respond(AsyncWebServerRequest* req) final;

    // 完成时由 response 主动通知 request 回收自己
    void finish();

protected:
    // 默认实现：header → OnDrain → pump → flush
    // 特殊子类（WebSocket）override
    virtual void doRespond();

    // 子类的唯一行为钩子：每次仅追加一块数据；完成时置 done_ = true
    // 默认：无 body
    virtual void pump() { done_ = true; }

    const char* responseCodeToString(uint16_t code);

    bool    sendContentLength_;
    bool    chunked_;
    int16_t code_;
    size_t  contentLength_;
    size_t  sentLength_;
    WebResponseState state_;
    std::string contentType_;
    LinkedList<AsyncWebHeader*> headers_;

    AsyncWebServerRequest* req_{nullptr};
    std::string            header_;
    bool                   done_{false};
};

#endif