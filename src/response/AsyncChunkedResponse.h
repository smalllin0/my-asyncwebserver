#ifndef ASYNCCHUNKEDRESPONSE_H_
#define ASYNCCHUNKEDRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncWebServerRequest;

class AsyncChunkedResponse : public AsyncWebServerResponse {
public:
    AsyncChunkedResponse(const std::string& contentType, AwsResponseFiller cb,
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