#include "AsyncCallbackWebHandler.h"
#include "../request/AsyncWebServerRequest.h"

#define TAG "AsyncCallbackWebHandler"


/// @brief 判断请求能否被处理
bool  AsyncCallbackWebHandler::canHandle(AsyncWebServerRequest* req)
{
    if (!onRequest_ || uri_.length() == 0) {
        return false;
    }
    if (!(method_ & req->method_)) {
        return false;
    }

#if CONFIG_ENABLE_REGEX
    if (isRegex_) {
        std::regex pattern(uri_.c_str());
        std::smatch matches;
        std::string str = req->url_;
        auto size = matches.size();
        if (std::regex_search(str, size, pattern)) {
            for (size_t i = 0; i < size; ++i) {
                req->addPathParam(matches[i].str().c_str());
            }
        } else {
            return false;
        }
    } else
#endif
    // 通配模式
    if (uri_.starts_with("/*.")) {
        // 包含特定结尾
        std::string uriTemplate = uri_.substr(uri_.find_last_of("."));
        if (!req->url_.ends_with(uriTemplate)) {
            return false;
        }
    } else {
        if (uri_.ends_with("*")) {
            // 包含特定开头
            std::string uriTemplate = uri_.substr(0, uri_.length() - 1);
            if (!req->url_.starts_with(uriTemplate)) {
                return false;
            }
        } else {
            // 完全匹配
            if (uri_ != req->url_ && !req->url_.starts_with(uri_ + "/")) {
                return false;
            }
        }
    }
    req->addInterestingHeader("ANY");

    return true;
}

/// @brief 进行身份认证后执行主请求处理回调
void AsyncCallbackWebHandler::handleRequest(AsyncWebServerRequest* req)
{
    if ((!username_.empty() && !password_.empty()) && 
        !req->authenticate(username_.c_str(), password_.c_str())) {
            return req->requestAuthentication();
    }
    if (onRequest_) {
        onRequest_(req);
    } else {
        req->send(500);
    }
}

/// @brief 文件上传处理
/// @param filename 客户端上传文件名
/// @param index 当前数据块在整个文件中的偏移字节数
/// @param data 当前上传数据块指针
/// @param len 当前数据块长度
/// @param final 是否为最后一个数据块
void AsyncCallbackWebHandler::handleUpload(AsyncWebServerRequest* req, const std::string &filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if ((!username_.empty() && !password_.empty()) && !req->authenticate(username_.c_str(), password_.c_str())) {
        return req->requestAuthentication();
    }
    if (onUpload_) {
        onUpload_(req, filename, index, data, len, final);
    }
}

/// @brief 对非上传请求体进行认证，以数据流方式传递给用户回调（适用于JSON、表单、二进制协议等）
/// @param data 当前数据块指针
/// @param len 当前数据块长度
/// @param index 当前块在整体中的偏移
/// @param total 整个body的长度
void AsyncCallbackWebHandler::handleBody(AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total)
{
    if ((!username_.empty() && !password_.empty()) && !req->authenticate(username_.c_str(), password_.c_str())) {
        return req->requestAuthentication();
    }
    if (onBody_) {
        onBody_(req, data, len, index, total);
    }
}

