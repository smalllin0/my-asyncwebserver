#ifndef ASYNCCALLBACKWEBHANDLER_H_
#define ASYNCCALLBACKWEBHANDLER_H_

#include "AsyncWebHandler.h"
#include <string>
#if CONFIG_ENABLE_REGEX
    #include <regex>
#endif
#include "../request/AsyncWebServerRequest.h"


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


using ArRequestHandlerFunction = std::function<void(AsyncWebServerRequest *request)>;
using ArUploadHandlerFunction = std::function<void(AsyncWebServerRequest *request, const std::string &filename, size_t index, uint8_t *data, size_t len, bool final)>;
using ArBodyHandlerFunction = std::function<void(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)>;


/// @brief 回调处理器（可用于动态文件响应）
/*
 * URI支持：
 * 1. 正则模式
 * 2. 通配模式（扩展名匹配：以*.开头；前缀匹配：以*结尾）
 * 3. 完全匹配模式
*/ 
class AsyncCallbackWebHandler : public AsyncWebHandler {
public:
    AsyncCallbackWebHandler(){}
    /// @brief 为处理器绑定目标URI（以^开头并且以$结尾时自动标识为正则模式）
    void setUri(const std::string &uri) {
        uri_ = uri;
        isRegex_ = uri.starts_with("^") && uri.ends_with("$");
    }
    /// @brief 设置处理器支持的HTTP方法
    void setMethod(WebRequestMethodComposite m) {
        method_ = m;
    }
    /// @brief 设置处理器请求主处理回调函数
    void onRequest(ArRequestHandlerFunction fn) {
        onRequest_ = fn;
    }
    /// @brief 设置文件上传处理回调函数
    void onUpload(ArUploadHandlerFunction fn) {
        onUpload_ = fn;
    }
    /// @brief 设置请求体处理回调函数
    void onBody(ArBodyHandlerFunction fn) {
        onBody_ = fn;
    }
    /// @brief 判断处理器是否为平凡处理器（无自定义处理函数）
    virtual bool isRequestHandlerTrivial() override final {
        return onRequest_ == nullptr;
    }
    virtual bool canHandle(AsyncWebServerRequest* req) override final;
    virtual void handleRequest(AsyncWebServerRequest* req) override final;
    virtual void handleUpload(AsyncWebServerRequest* req, const std::string &filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    virtual void handleBody(AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total) override final;


protected:
    std::string                 uri_{empty_string};     // 处理器绑定的URI目录
    WebRequestMethodComposite   method_{HTTP_ANY};      // 支持的HTTP方法
    ArRequestHandlerFunction    onRequest_{nullptr};    // 主请求处理回调
    ArUploadHandlerFunction     onUpload_{nullptr};     // 文件上传处理回调
    ArBodyHandlerFunction       onBody_{nullptr};       // 请求体处理回调
    bool                        isRegex_{false};        // 标识URI是否为正则模式       
};

#endif