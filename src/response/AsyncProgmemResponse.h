#ifndef ASYNCPROGMEMRESPONSE_H_
#define ASYNCPROGMEMRESPONSE_H_

#include "AsyncAbstractResponse.h"
#include <string>

/// 以内部存储器为响应
class AsyncProgmemResponse : public AsyncAbstractResponse {
public:
    AsyncProgmemResponse(int code, const std::string& contentType, const uint8_t* content, size_t len, AwsTemplateProcessor callback=nullptr);
    inline bool sourceValid() const;
    virtual size_t fillBuffer(uint8_t* buf, size_t maxLen) override;
private:
    const uint8_t*  content_;
    size_t          readLength_;
};

#endif