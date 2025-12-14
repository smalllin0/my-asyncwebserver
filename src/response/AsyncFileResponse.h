#ifndef ASYNCFILERESPONSE_H_
#define ASYNCFILERESPONSE_H_

#include "AsyncAbstractResponse.h"
#include <string>
#include <stdio.h>

class AsyncFileResponse : public AsyncAbstractResponse {
public:
    AsyncFileResponse(std::string path, std::string contentType=empty_string, bool download=false, AwsTemplateProcessor cb=nullptr);
    ~AsyncFileResponse();
    inline bool sourceValid() const {
        return file_ != nullptr;
    }
    inline virtual size_t fillBuffer(uint8_t* buf, size_t maxLen) override {
        return fread(buf, sizeof(uint8_t), maxLen, file_);
    }
private:
    void setContentType(const std::string& path);

    FILE        *file_;
    std::string path_;
};

#endif