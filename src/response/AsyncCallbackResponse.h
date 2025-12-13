#ifndef ASYNCCALLBACKRESPONSE_H_
#define ASYNCCALLBACKRESPONSE_H_

#include "AsyncAbstractResponse.h"
#include <string>

class AsyncCallbackResponse : public AsyncAbstractResponse {
public:
    AsyncCallbackResponse(const std::string &contentType, size_t len, AwsResponseFiller cb, AwsTemplateProcessor templateCb = nullptr);
    inline bool sourceValid() const;
    virtual size_t fillBuffer(uint8_t* buf, size_t maxLen) override;
private:
    AwsResponseFiller   content_;
    size_t              filledLength_;
};

#endif