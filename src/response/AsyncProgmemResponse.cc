#include "AsyncProgmemResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"

AsyncProgmemResponse::AsyncProgmemResponse(int code, const std::string &contentType,
                                           const uint8_t *content, size_t len,
                                           AwsTemplateProcessor callback)
    : content_(content)
{
    code_ = code;
    contentType_ = contentType;
    contentLength_ = len;
}

void AsyncProgmemResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) return;
    state_ = RESPONSE_CONTENT;

    req_ = req;
    client_ = req->client();
    header_ = assembleHead(req->version());

    client_->Append(header_.c_str(), header_.length(),
                    TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    // ★ content_ 在 progmem，生命周期无限长 —— 不用 COPY
    client_->Append(content_, contentLength_, 0);

    client_->OnDrain([](void* ctx) {
        static_cast<AsyncProgmemResponse*>(ctx)->finish();
    }, this);

    client_->Flush();
}
