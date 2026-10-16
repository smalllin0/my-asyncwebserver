#include "AsyncBasicResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"
#include <string>

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

void AsyncBasicResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) return;
    state_ = RESPONSE_CONTENT;

    req_ = req;
    client_ = req->client();
    header_ = assembleHead(req->version());

    // ★ COPY：response 会在 OnDrain 后删除，必须保证数据独立
    client_->Append(header_.c_str(), header_.length(),
                    TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);
    client_->Append(content_.c_str(), content_.length(),
                    TCP_WRITE_FLAG_COPY);

    client_->OnDrain([](void* ctx) {
        static_cast<AsyncBasicResponse*>(ctx)->finish();
    }, this);

    client_->Flush();
}
