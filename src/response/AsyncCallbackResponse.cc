#include "AsyncCallbackResponse.h"
#include "../request/AsyncWebServerRequest.h"

AsyncCallbackResponse::AsyncCallbackResponse(const std::string& contentType, size_t len, AwsResponseFiller cb, AwsTemplateProcessor templateCb)
{
    code_ = 200;
    content_ = cb;
    contentLength_ = len;

    if(len == 0) {
        sendContentLength_ = false;
    }
    contentType_ = contentType;
    filledLength_ = 0;
}

inline bool AsyncCallbackResponse::sourceValid() const 
{
    return content_ != nullptr;
}

size_t AsyncCallbackResponse::fillBuffer(uint8_t* buf, size_t maxLen)
{
    size_t ret = content_(buf, maxLen, filledLength_);
    if (ret != RESPONSE_TRY_AGAIN) {
        filledLength_ += ret;
    }

    return ret;
}
