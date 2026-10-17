// AsyncProgmemResponse.cc
#include "AsyncProgmemResponse.h"
#include "../request/AsyncWebServerRequest.h"

AsyncProgmemResponse::AsyncProgmemResponse(int code, const std::string& contentType,
                                           const uint8_t* content, size_t len,
                                           AwsTemplateProcessor /*callback*/)
    : content_(content)
{
    code_ = code;
    contentType_ = contentType;
    contentLength_ = len;
}

void AsyncProgmemResponse::pump()
{
    if (done_) return;
    req_->append(content_, contentLength_);
    done_ = true;
}
