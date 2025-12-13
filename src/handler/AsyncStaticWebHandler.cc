#include "AsyncStaticWebHandler.h"
#include "../request/AsyncWebServerRequest.h"
#include "../response/AsyncBasicResponse.h"
#include "../response/AsyncFileResponse.h"
#include <string.h>

#define TAG "AsyncStaticWebHandler"



/// @brief 构造一个静态文件处理器
/// @param uri 绑定的URI前缀
/// @param path 响应文件在文件系统中的路径
/// @param cache_control 缓存控制行为
/// new AsyncStaticWebHandler("/", "/spiffs/", "public, max-age=31536000");
AsyncStaticWebHandler::AsyncStaticWebHandler(const char* uri, const char* path, const char* cache_control)
    : cache_control_(cache_control)
{
    auto uri_len = strlen(uri);
    if (uri_len) {
        uri_ = (uri[0] == '/') ? uri : (std::string("/") + uri);
    } else {
        uri_ = "/";
    }

    auto path_len = strlen(path);
    if (path_len) {
        path_ = (path[0] == '/') ? path : (std::string("/") + path);
    } else {
        path_ = "/";
    }
    isDir_ = (path[path_len - 1] == '/');


    if (uri_[uri_len - 1] == '/') {
        uri_.pop_back();
    }
    if (path_[path_len - 1] == '/') {
        path_.pop_back();
    }
}

/// @brief 检查请求是否能够被本处理器处理，可处理时会在req中标记感兴趣的头部信息
/// @param req 请求
bool AsyncStaticWebHandler::canHandle(AsyncWebServerRequest* req)
{
    if ((req->method_ != HTTP_GET) 
            || !req->url_.starts_with(uri_)
            || !req->isExpectedRequestedConnType(RCT_DEFAULT, RCT_HTTP)) {
        return false;
    }

    if (getFile(req)) {
        if (last_modified_.length()) {
            req->addInterestingHeader("If-Modified-Since");
        }
        if (cache_control_.length()) {
            req->addInterestingHeader("If-None-Match");
        }
        return true;
    }

    return false;
}


void AsyncStaticWebHandler::handleRequest(AsyncWebServerRequest* req)
{
    if (username_.length() && password_.length() 
        && req->authenticate(username_.c_str(), password_.c_str())) {
            req->requestAuthentication();
    }

    std::string file_name = (char*)(req->tmpObj_);
    struct stat file_stat;
    if (stat((char*)(req->tmpObj_), &file_stat) != -1) {
        auto etag = std::to_string(file_stat.st_size);
        if (last_modified_.length() && last_modified_ == req->header("If-Modified-Since")) {
            req->send(304);
        } else if (cache_control_.length() && req->hasHeader("If-None-Match") 
            && (req->header("If-None-Match") == etag)) {
                auto* response = new AsyncBasicResponse(304);
                response->addHeader("Cache-Control", cache_control_);
                response->addHeader("ETag", etag);
                req->send(response);
        } else {
            auto* response = new AsyncFileResponse(file_name, empty_string, false, callback_);
            if (last_modified_.length()) {
                response->addHeader("Last-Modified", last_modified_);
            }
            if (cache_control_.length()) {
                response->addHeader("Cache-Control", cache_control_);
                response->addHeader("ETag", etag);
            }
            req->send(response);
        }
    } else {
        req->send(404);
    }

    free(req->tmpObj_);
    req->tmpObj_ = nullptr;
}


/// @brief 检查req中的文件是否存在，存在时将存于req->tmpObj中
bool AsyncStaticWebHandler::getFile(AsyncWebServerRequest* req)
{
    const std::string& url = req->url_;
    const auto prefixLen = uri_.length();
    
    auto pathView = url.substr(prefixLen);

    bool pathEmpty = pathView.length() == 0;
    bool endsWithSlash = (!pathEmpty && pathView[pathView.length() - 1] == '/');
    bool shouldSkipCheck = (isDir_ && pathEmpty) || endsWithSlash;

    // 先尝试原路径文件
    if (!shouldSkipCheck) {
        auto fullPath = path_ + pathView;
        if (fileExists(req, fullPath)) {
            return true;
        }
    }

    // 检查默认文件是否设置
    if (default_file_.length() == 0) {
        return false;
    }

    // 构建默认文件路径
    auto fullPath = path_ + pathView;
    if (pathEmpty || fullPath[fullPath.length() - 1] != '/') {
        fullPath += '/';
    }
    fullPath += default_file_;

    return fileExists(req, fullPath);
}

/// @brief 检查指定路径的文件是否存在（存在时文件路径存在于req->tmpObj_），并根据请求次数设置优先发送gzip文件标志
bool AsyncStaticWebHandler::fileExists(AsyncWebServerRequest* req, const std::string& path)
{
    auto fileFound = false;
    auto gzipFound = false;
    auto gzip = path + ".gz";

    if (gzipFirst_) {
        gzipFound = FILE_IS_REAL(gzip.c_str());
        if (!gzipFound) {
            fileFound = FILE_IS_REAL(path.c_str());
        }
    } else {
        fileFound = FILE_IS_REAL(path.c_str());
        if (!fileFound) {
            gzipFound = FILE_IS_REAL(gzip.c_str());
        }
    }
    bool found = fileFound || gzipFound;

    if (found) {
        auto pathLen = path.length() + 1;
        auto* tmpPath = (char*) malloc(pathLen);
        snprintf(tmpPath, pathLen, "%s", path.c_str());
        req->tmpObj_ = (void*)tmpPath;

        gzipStats_ = (gzipStats_ << 1) + (gzipFound ? 1 : 0);
        if (gzipStats_ == 0x00) {
            gzipFirst_ = false;
        } else if (gzipStats_ == 0xff) {
            gzipFirst_ = true;
        } else {
            gzipFirst_ = countBits(gzipStats_) > 4;
        }
    }
    return found;
}

/// @brief 计算给定数据的二进制中1所占的个数
inline uint8_t AsyncStaticWebHandler::countBits(const uint8_t value) const
{
    uint8_t w = value;
    uint8_t i;
    for (i = 0; w != 0; i++) {
        w &= w - 1;
    }

    return i;
}

/// @brief 检查文件系统中是否存在指定的的文件
bool AsyncStaticWebHandler::FILE_IS_REAL(const char* path) const
{
    struct stat path_stat;
    if (stat(path, &path_stat) == -1) {
        return false;
    }
    if (S_ISDIR(path_stat.st_mode)) {
        return false;
    }
    return true;
}