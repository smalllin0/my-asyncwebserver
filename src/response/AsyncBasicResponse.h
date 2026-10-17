// AsyncBasicResponse.h
#ifndef ASYNCBASICRESPONSE_H_
#define ASYNCBASICRESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>

class AsyncBasicResponse : public AsyncWebServerResponse {
public:
    AsyncBasicResponse(uint16_t code, const std::string& contentType = empty_string,
                       const std::string& content = empty_string);
    bool sourceValid() const override { return true; }

protected:
    void pump() override;

private:
    std::string content_;
};

#endif
