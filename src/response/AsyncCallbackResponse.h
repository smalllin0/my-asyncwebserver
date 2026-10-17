// AsyncCallbackResponse.h
#ifndef ASYNCCALLBACKRESPONSE_H_
#define ASYNCCALLBACKRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncWebServerRequest;

class AsyncCallbackResponse : public AsyncWebServerResponse {
public:
    AsyncCallbackResponse(const std::string& contentType, size_t len,
                          AwsResponseFiller cb,
                          AwsTemplateProcessor templateCb = nullptr);
    ~AsyncCallbackResponse() override;
    bool sourceValid() const override { return content_ != nullptr; }

protected:
    void pump() override;

private:
    AwsResponseFiller  content_;
    size_t             filledLength_{0};
    uint8_t*           buffer_{nullptr};    //  全程复用
};

#endif