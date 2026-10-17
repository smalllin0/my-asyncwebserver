// AsyncChunkedResponse.h
#ifndef ASYNCCHUNKEDRESPONSE_H_
#define ASYNCCHUNKEDRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncWebServerRequest;

class AsyncChunkedResponse : public AsyncWebServerResponse {
public:
    AsyncChunkedResponse(const std::string& contentType, AwsResponseFiller cb,
                         AwsTemplateProcessor templateCb = nullptr);
    ~AsyncChunkedResponse() override;
    bool sourceValid() const override { return content_ != nullptr; }

protected:
    void pump() override;

private:
    AwsResponseFiller  content_;
    size_t             filledLength_{0};
    uint8_t*           buffer_{nullptr};    //  全程复用，含帧头/帧尾
};

#endif