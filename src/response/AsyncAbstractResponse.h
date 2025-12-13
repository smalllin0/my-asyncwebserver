#ifndef ASYNCABSTRACTRESPONSE_H_
#define ASYNCABSTRACTRESPONSE_H_

#include <string>
#include <vector>
#include "AsyncWebServerResponse.h"

class AsyncWebServerRequest;

/// @brief 派生自定义响应类型的一个抽象基类：支持动态内容生成、模板替换，并通过缓存机制实现流式、分块发送响应体。
class AsyncAbstractResponse : public AsyncWebServerResponse {
public:
    AsyncAbstractResponse(AwsTemplateProcessor cb = nullptr);
    void respond(AsyncWebServerRequest* req);
    size_t ack(AsyncWebServerRequest* req, size_t len, uint32_t time);
    bool sourceValid() const {
        return false;
    }
    virtual size_t fillBuffer(uint8_t* buf , size_t max_len ) {
        return 0;
    }
protected:
    AwsTemplateProcessor    callback_;  //模板回调
private:    
    size_t  readDataFromCacheOrContent(uint8_t* data, const size_t len);
    size_t  fillBufferAndProcessTemplates(uint8_t* buf, size_t max_len);

    size_t                  headerSent_;// 响应头部已发送长度
    std::string             header_;    // 存放组装好的响应头部
    std::vector<uint8_t>    cache_;     // 响应数据缓存
    std::vector<uint8_t>    buffer_;    // 用于临时保存待发送的响应体的缓冲区
};




#endif