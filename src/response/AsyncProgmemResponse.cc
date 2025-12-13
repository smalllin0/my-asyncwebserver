#include "AsyncProgmemResponse.h"

AsyncProgmemResponse::AsyncProgmemResponse(int code, const std::string &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback)
{
    code_ = code;
    content_ = content;
    contentType_ = contentType;
    contentLength_ = len;
    readLength_ = 0;
}

inline bool AsyncProgmemResponse::sourceValid() const
{
    return true;
}

size_t AsyncProgmemResponse::fillBuffer(uint8_t* buf, size_t maxLen)
{
    size_t left = contentLength_ - readLength_;
    if (left > maxLen) {
        memcpy(buf, content_ + readLength_, maxLen);
        readLength_ += maxLen;
        return maxLen;
    }
    memcpy(buf, content_ + readLength_, left);
    readLength_ += left;
    return left;
}
