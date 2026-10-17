// AsyncProgmemResponse.h
#ifndef ASYNCPROGMEMRESPONSE_H_
#define ASYNCPROGMEMRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncProgmemResponse : public AsyncWebServerResponse {
public:
    AsyncProgmemResponse(int code, const std::string& contentType,
                         const uint8_t* content, size_t len,
                         AwsTemplateProcessor callback = nullptr);
    bool sourceValid() const override { return content_ != nullptr; }

protected:
    void pump() override;

private:
    const uint8_t*  content_;
};

#endif