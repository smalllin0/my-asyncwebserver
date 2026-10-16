#ifndef ASYNCBASICRESPONSE_H_
#define ASYNCBASICRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncWebServerRequest;

class AsyncBasicResponse : public AsyncWebServerResponse {
public:
    AsyncBasicResponse(uint16_t code, const std::string& contentType = empty_string,
                       const std::string& content = empty_string);
    void respond(AsyncWebServerRequest* req) override;
    bool sourceValid() const override { return true; }

private:
    std::string content_;
};

#endif
