#ifndef ASYNCCHUNKEDRESPONSE_H_
#define ASYNCCHUNKEDRESPONSE_H_

#include "AsyncAbstractResponse.h"
#include <string>
#include "../request/AsyncWebServerRequest.h"

class AsyncChunkedResponse : public AsyncAbstractResponse {
public:
    AsyncChunkedResponse(const std::string &contentType, AwsResponseFiller cb, AwsTemplateProcessor templateCb = nullptr);
    inline bool sourceValid() const {
        return content_ != nullptr;
    }
    virtual size_t fillBuffer(uint8_t* buf, size_t maxLen) override {
        auto ret = content_(buf, maxLen, filledLength_);
        if (ret != RESPONSE_TRY_AGAIN) {
            filledLength_ += ret;
        }

        return ret;
    }
private:
    AwsResponseFiller content_;
    size_t filledLength_;
};

#endif