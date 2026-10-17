// AsyncBasicResponse.cc
#include "AsyncBasicResponse.h"
#include "../request/AsyncWebServerRequest.h"

AsyncBasicResponse::AsyncBasicResponse(uint16_t code, const std::string& contentType,
                                       const std::string& content)
    : AsyncWebServerResponse(code, contentType)
    , content_(content)
{
    contentLength_ = content_.length();
    if (contentLength_ && contentType_.empty()) {
        contentType_ = "text/plain";
    }
    addHeader("Connection", "close");
}

void AsyncBasicResponse::pump()
{
    if (done_) return;
    req_->append(content_.c_str(), content_.length(), TCP_WRITE_FLAG_COPY);
    done_ = true;
}
