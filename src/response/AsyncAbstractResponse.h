#ifndef ASYNCABSTRACTRESPONSE_H_
#define ASYNCABSTRACTRESPONSE_H_

#include <string>
#include <vector>
#include "AsyncWebServerResponse.h"

class AsyncWebServerRequest;

class AsyncAbstractResponse : public AsyncWebServerResponse {
public:
    AsyncAbstractResponse(AwsTemplateProcessor cb = nullptr);
    void respond(AsyncWebServerRequest* req) override;
    bool sourceValid() const override { return false; }

protected:
    AwsTemplateProcessor callback_;

    /// @brief 从派生类读取原始数据到 buf，返回读取字节数
    /// @note 派生类必须实现（文件/回调/生成器）
    virtual size_t fillBuffer(uint8_t* buf, size_t max_len) = 0;

private:
    size_t readDataFromCacheOrContent(uint8_t* data, size_t len);
    void   pump();
    void   processTemplates(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out);

    std::vector<uint8_t>  cache_;
    bool                  done_{false};
};

#endif