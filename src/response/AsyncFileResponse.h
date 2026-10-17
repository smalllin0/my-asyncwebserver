// AsyncFileResponse.h
#ifndef ASYNCFILERESPONSE_H_
#define ASYNCFILERESPONSE_H_

#include "AsyncWebServerResponse.h"
#include <string>
#include <stdio.h>

class AsyncWebServerRequest;

class AsyncFileResponse : public AsyncWebServerResponse {
public:
    AsyncFileResponse(std::string path, std::string contentType = empty_string,
                      bool download = false, AwsTemplateProcessor cb = nullptr);
    ~AsyncFileResponse() override;
    bool sourceValid() const override { return file_ != nullptr; }

protected:
    void pump() override;

private:
    void setContentType(const std::string& path);

    FILE*           file_{nullptr};
    std::string     path_;
    size_t          readLength_{0};
    uint8_t*        buffer_{nullptr};    //  全程复用
};

#endif