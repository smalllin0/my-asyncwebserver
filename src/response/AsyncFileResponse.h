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
    ~AsyncFileResponse();
    void respond(AsyncWebServerRequest* req) override;
    bool sourceValid() const override { return file_ != nullptr; }

private:
    void setContentType(const std::string& path);
    void pump();

    FILE*           file_{nullptr};
    std::string     path_;
    size_t          readLength_{0};
};

#endif