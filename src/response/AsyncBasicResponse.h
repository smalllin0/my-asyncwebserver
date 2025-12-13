#ifndef ASYNCBASICRESPONSE_H_
#define ASYNCBASICRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <vector>
#include <string>


class AsyncWebServerRequest;


class AsyncBasicResponse : public AsyncWebServerResponse {
public:
    AsyncBasicResponse(uint16_t code, const std::string& contentType=empty_string, const std::string& content=empty_string);
    size_t ack(AsyncWebServerRequest* req, size_t len, uint32_t time) override;
    virtual void respond(AsyncWebServerRequest* req) override;
    inline bool sourceValid() const override {
        return true;
    }
private:
    size_t      headerSent_;    // 响应头中已发送的数据
    size_t      countentSent_;  // 响应体中已发送的数据
    std::string header_;        // 要发送的响应头部
    std::string content_;       // 响应要发送的内容（响应体）：content
};

#endif // !ASYNCBASICRESPONSE_H_

