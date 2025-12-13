#ifndef ASYNCWEBHANDLER_H_
#define ASYNCWEBHANDLER_H_


#include <functional>
#include <string>



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

using ArRequestFilterFunction = std::function<bool(AsyncWebServerRequest* req)>;

class AsyncWebHandler {
public:
    AsyncWebHandler(){}
    virtual ~AsyncWebHandler(){}

    AsyncWebHandler& setFilter(ArRequestFilterFunction fn) {
        filter_ = fn;
        return *this;
    }
    AsyncWebHandler& setAuthentication(const char* name, const char* passwd) {
        username_ = name;
        password_ = passwd;
        return *this;
    }
    /// @brief 是否通过过滤器检测
    bool filter(AsyncWebServerRequest* req) {
        return filter_ == nullptr || filter_(req);
    }
    virtual bool isRequestHandlerTrivial() {
        return true;
    }
    virtual bool canHandle(AsyncWebServerRequest* req [[maybe_unused]]) { return false; }
    virtual void handleRequest(AsyncWebServerRequest* req [[maybe_unused]]) {}
    virtual void handleUpload(AsyncWebServerRequest *request  [[maybe_unused]],
                              const std::string &filename [[maybe_unused]],
                              size_t index [[maybe_unused]],
                              uint8_t *data [[maybe_unused]],
                              size_t len [[maybe_unused]],
                              bool final  [[maybe_unused]]){}
    virtual void handleBody(AsyncWebServerRequest *request [[maybe_unused]],
                            uint8_t *data [[maybe_unused]],
                            size_t len [[maybe_unused]],
                            size_t index [[maybe_unused]],
                            size_t total [[maybe_unused]]){}

protected:
    std::string             username_{};
    std::string             password_{};
    ArRequestFilterFunction filter_;
};

#endif // !ASYNCWEBHANDLER_H_