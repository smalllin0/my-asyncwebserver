#ifndef ASYNCSTATICWEBHANDLER_H_
#define ASYNCSTATICWEBHANDLER_H_

#include "AsyncWebHandler.h"
#include <string>
#include "time.h"
#include "../tools.h"


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


using AwsTemplateProcessor = std::function<std::string(const std::string& )>;


/*
 * 请求URL：http://localhost/static/css/style.css
 * 真空文件：/spiffs/www/css/style.css
 * 则处理器可绑定URI=/static/
 * 处理器中资源目录应当设置为：/spiffs/www/
*/

/// 静态资源处理器
class AsyncStaticWebHandler : public AsyncWebHandler {
public:
    AsyncStaticWebHandler(const char* uri, const char* path, const char* cache_control);
    virtual bool canHandle(AsyncWebServerRequest* req) override final;
    virtual void handleRequest(AsyncWebServerRequest* req) override final;
    /// @brief 标记当前URI为目录
    inline AsyncStaticWebHandler& setIsDir(bool isDir) {
        isDir_ = isDir;
        return *this;
    }
    /// @brief 设置当前URI的默认文件
    inline AsyncStaticWebHandler& setDefaultFile(const char* filename) {
        default_file_ = filename;
        return *this;
    }
    inline AsyncStaticWebHandler& setCacheControl(const char* cache_control) {
        cache_control_ = cache_control;
        return *this;
    }
    /// @brief 设置资源 Last-Modified 的数值（上次修改时间）
    inline AsyncStaticWebHandler& setLastModified(const char* last_modified) {
        last_modified_ = last_modified;
        return *this;
    }
    /// @brief 设置资源 Last-Modified 的数值（上次修改时间）
    inline AsyncStaticWebHandler& setLastModified(struct tm* last_modified) {
        char result[30];
        strftime(result, 30, "%a, %d %b %Y %H:%M:%S %Z", last_modified);
        return setLastModified((const char*)result);
    }
    /// @brief 设置当前URI的模板处理函数
    inline AsyncStaticWebHandler& setTemplateProcessor(AwsTemplateProcessor cb) {
        callback_ = cb;
        return *this;
    }
protected:
    std::string     uri_;                           // 处理器绑定的根路径URI目录(已去除末尾的/)
    std::string     path_;                          // 文件系统中的资源目录(已去除末尾的/)
    std::string     default_file_{"index.htm;"};    // 默认文件
    std::string     cache_control_;                 // 缓存控制头（如max-age=3600}
    std::string     last_modified_{empty_string};   // 最后修改时间
    bool            isDir_{false};                  // 标记处理的URI是否为目录
    bool            gzipFirst_{false};              // 是否优先查找gzip文件
    uint8_t         gzipStats_{0xF8};               // GZIP查找统计（8位位图）
    AwsTemplateProcessor    callback_{nullptr};     //
private:
    bool    getFile(AsyncWebServerRequest* req);
    bool    fileExists(AsyncWebServerRequest* req, const std::string& path);
    inline uint8_t countBits(const uint8_t value) const;
    bool    FILE_IS_REAL(const char* path) const;
};

#endif  