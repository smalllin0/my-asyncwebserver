#ifndef ASYNCWEBREWRITE_H_
#define ASYNCWEBREWRITE_H_

#include <functional>
#include <string>
#include "../request/AsyncWebServerRequest.h"

using ArRequestFilterFunction = std::function<bool(AsyncWebServerRequest* req)>;

class AsyncWebRewrite {
public:
    AsyncWebRewrite(const char* from, const char* to);
    virtual ~AsyncWebRewrite(){}

    AsyncWebRewrite& setFilter(ArRequestFilterFunction fn) {
        filter_ = fn;
        return *this;
    }
    bool filter(AsyncWebServerRequest* req) const {
        return filter_ == nullptr || filter_(req);
    }
    const std::string& from() const {
        return from_;
    }
    const std::string& toUrl() const {
         return to_;
    }
    const std::string& params() const {
        return params_;
    }
    virtual bool match(AsyncWebServerRequest* req) {
        return from_ == req->url_ && filter(req);
    }

protected:
    std::string     from_;                  // 原始URL
    std::string     to_;                    // 重定向后URL
    std::string     params_;                // 附加在目标URL后的参数
    ArRequestFilterFunction     filter_;    // 过滤函数
};

#endif