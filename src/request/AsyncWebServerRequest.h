#ifndef ASYNCWEBSERVERREQUEST_H_
#define ASYNCWEBSERVERREQUEST_H_

#include <functional>
#include <string>
#include "../StringArray.h"
#include "lwip/err.h"
#include "../handler/AsyncStaticWebHandler.h"
#include "AsyncConnection.h"

class AsyncConnection;
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
using ArDisconnectHandler = void(*)(void);

enum RequestedConnectionType {
    RCT_NOT_USED = -1,
    RCT_DEFAULT  = 0,
    RCT_HTTP,
    RCT_WS,
    RCT_EVENT,
    RCT_MAX
};

using AwsResponseFiller = std::function<size_t(uint8_t* buffer, size_t maxLen, size_t index)>;

class AsyncWebServerRequest {
public:
    AsyncWebServerRequest();

    void onDisconnect(ArDisconnectHandler fn) { onDisconnectfn_ = fn; }

    // ---------- public 查询接口（响应类会用到） ----------
    AsyncConnection* client()  { return client_; }
    uint8_t          version() const { return version_; }
    WebRequestMethodComposite method() const { return method_; }
    const std::string& url()         const { return url_; }
    const std::string& host()        const { return host_; }
    const std::string& contentType() const { return contentType_; }
    size_t contentLength() const { return contentLength_; }
    bool   multipart()     const { return isMultipart_; }
    RequestedConnectionType requestedConnType() const { return reqconntype_; }

    void setHandler(AsyncWebHandler* handler) { handler_ = handler; }

    size_t headers() const { return headers_.length(); }
    size_t params()  const { return params_.length(); }
    size_t args()    const { return params(); }

    const char* methodToString() const;
    const char* requestedConnTypeToString() const;
    bool isExpectedRequestedConnType(RequestedConnectionType erct1,
                                     RequestedConnectionType erct2 = RCT_NOT_USED,
                                     RequestedConnectionType erct3 = RCT_NOT_USED);

    bool hasHeader(const std::string &name) const;
    bool hasParam(const std::string &name, bool post = false, bool file = false) const;
    bool hasArg(const char* name) const;
    AsyncWebHeader*    getHeader(const std::string &name) const;
    AsyncWebHeader*    getHeader(size_t index) const;
    AsyncWebParameter* getParam(const std::string &name, bool post = false, bool file = false) const;
    AsyncWebParameter* getParam(size_t index) const;
    const std::string& arg(const std::string& name) const;
    const std::string& arg(size_t index) const;
    const std::string& argName(size_t index) const;
    const std::string& header(const char* name) const;
    const std::string& header(size_t index) const;
    const std::string& headerName(size_t index) const;

    bool authenticate(const char* hash);
    bool authenticate(const char* username, const char* passwd,
                      const char* realm = nullptr, bool passwdIsHash = false);
    void requestAuthentication(const char* realm = nullptr, bool isDigest = true);

    void addInterestingHeader(std::string name);
    void redirect(std::string url);

    // ---------- 响应发送 ----------
    void send(AsyncWebServerResponse* response);

    void send(int code, std::string contentType = "", std::string content = "") {
        send(beginResponse(code, std::move(contentType), std::move(content)));
    }
    void send(std::string path, std::string contentType = "",
              bool download = false, AwsTemplateProcessor callback = nullptr);
    void sendChunked(std::string contentType, AwsResponseFiller callback,
                     AwsTemplateProcessor templateCallback = nullptr);
    void send_P(int code, std::string contentType, const uint8_t* content, size_t len,
                AwsTemplateProcessor callback = nullptr);

    AsyncWebServerResponse* beginResponse(int code, std::string contentType = "",
                                          std::string content = "");
    AsyncWebServerResponse* beginResponse(std::string path, std::string contentType = "",
                                          bool download = false,
                                          AwsTemplateProcessor callback = nullptr);
    AsyncWebServerResponse* beginResponse(std::string contentType, size_t len,
                                          AwsResponseFiller callback,
                                          AwsTemplateProcessor templateCallback = nullptr);
    AsyncWebServerResponse* beginChunkedResponse(std::string contentType,
                                                 AwsResponseFiller callback,
                                                 AwsTemplateProcessor templateCallback = nullptr);
    AsyncWebServerResponse* beginResponse_P(int code, std::string contentType,
                                            const uint8_t *content, size_t len,
                                            AwsTemplateProcessor callback = nullptr);
    void send_P(int code, std::string contentType,
            const uint8_t* content, size_t len,
            const char* contentEncoding,      // ★ 新增参数
            AwsTemplateProcessor callback = nullptr);

    // ★ 改为声明（实现在 .cc 里，那时 AsyncWebServerResponse 是完整类型）
    void finishResponse();

private:
    friend class AsyncWebServer;
    friend class AsyncWebHandler;
    friend class AsyncCallbackWebHandler;
    friend class AsyncStaticWebHandler;
    friend class AsyncWebServerResponse;
    friend class AsyncBasicResponse;
    friend class AsyncAbstractResponse;
    friend class AsyncWebRewrite;
    friend class DefaultHeaders;
    friend class AsyncWebSocketResponse;
    friend class AsyncWebSocketClient;
    friend class AsyncWebSocket;

    ~AsyncWebServerRequest() { reset(); }

    void init(AsyncWebServer* server, AsyncConnection* client);
    void reset();

    inline void onPoll();
    inline void onErr(err_t error);
    inline void onData(void* buf, size_t len);
    void onDisconnect();

    void addParam(AsyncWebParameter* p) { params_.add(p); }
    void addPathParam(const char* param) { pathParams_.add(new std::string(param)); }

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
    AsyncWebServerRequest*  next_;
    AsyncConnection*        client_;
    AsyncWebServer*         server_;
    AsyncWebHandler*        handler_{nullptr};
    AsyncWebServerResponse* response_{nullptr};
    StringArray             interestingHeaders_;
    ArDisconnectHandler     onDisconnectfn_{nullptr};

    LinkedList<AsyncWebHeader*>     headers_;
    LinkedList<AsyncWebParameter*>  params_;
    LinkedList<std::string*>        pathParams_;

    std::string             tmp_{};
    bool                    isFragmented_{false};
    uint8_t                 parseState_;

    uint8_t                     version_{1};
    WebRequestMethodComposite   method_{HTTP_ANY};
    std::string                 url_{};
    std::string                 host_{};
    std::string                 contentType_{};
    std::string                 boundary_{};
    std::string                 authorization_{};
    RequestedConnectionType     reqconntype_{RCT_HTTP};

    bool        isDigest_{false};
    bool        isMultipart_{false};
    bool        isPlainPost_{false};
    bool        expectingContinue_{false};
    size_t      contentLength_{0};
    size_t      parsedLength_{0};
    size_t      boundaryMatchLen_{0};

    uint8_t     multiParseState_{0};
    uint8_t     boundaryPosition_{0};
    bool        itemIsFile_{false};
    size_t      itemStartIndex_{0};
    size_t      itemSize_{0};
    size_t      itemBufferIndex_{0};
    uint8_t*    itemBuffer_{nullptr};
    std::string itemName_{};
    std::string itemFileName_{};
    std::string itemType_{};
    std::string itemValue_{};
};

#endif