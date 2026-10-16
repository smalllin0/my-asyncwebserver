#ifndef ASYNCPROGMEMRESPONSE_H_
#define ASYNCPROGMEMRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncWebServerRequest;

class AsyncProgmemResponse : public AsyncWebServerResponse {
public:
    AsyncProgmemResponse(int code, const std::string& contentType,
                         const uint8_t* content, size_t len,
                         AwsTemplateProcessor callback = nullptr);
    void respond(AsyncWebServerRequest* req) override;
    bool sourceValid() const override { return content_ != nullptr; }

private:
    const uint8_t*  content_;
};

#endif