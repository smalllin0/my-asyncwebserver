// AsyncAbstractResponse.h
#ifndef ASYNCABSTRACTRESPONSE_H_
#define ASYNCABSTRACTRESPONSE_H_

#include <string>
#include <vector>
#include "AsyncWebServerResponse.h"

class AsyncWebServerRequest;

class AsyncAbstractResponse : public AsyncWebServerResponse {
public:
    AsyncAbstractResponse(AwsTemplateProcessor cb = nullptr);
    ~AsyncAbstractResponse() override;
    bool sourceValid() const override { return true; }

protected:
    void pump() override;

    // 子类实现：从数据源读最多 max_len 字节
    virtual size_t fillBuffer(uint8_t* buf, size_t max_len) = 0;

    AwsTemplateProcessor callback_;

private:
    void processTemplates(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out);

    uint8_t*             buffer_{nullptr};   // 原始数据缓冲，一次分配
    std::vector<uint8_t> outBuf_;            // 模板输出复用
};

#endif