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
    void respond(AsyncWebServerRequest* req) override;
    bool sourceValid() const override { return content_ != nullptr; }

private:
    void pump();

    AwsResponseFiller  content_;
    size_t             filledLength_{0};
    bool               done_{false};
};

#endif