#include "AsyncChunkedResponse.h"

AsyncChunkedResponse::AsyncChunkedResponse(const std::string &contentType, AwsResponseFiller cb, AwsTemplateProcessor templateCb)
{
    code_ = 200;
    content_ = cb;
    contentLength_ = 0;
    contentType_ = contentType;
    sendContentLength_ = false;
    chunked_ = true;
    filledLength_ = 0;
}