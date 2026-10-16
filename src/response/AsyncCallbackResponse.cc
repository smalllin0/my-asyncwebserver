#include "AsyncCallbackResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"

AsyncCallbackResponse::AsyncCallbackResponse(const std::string& contentType, size_t len,
                                             AwsResponseFiller cb,
                                             AwsTemplateProcessor templateCb)
    : content_(cb)
{
    code_ = 200;
    contentLength_ = len;
    if (len == 0) {
        sendContentLength_ = false;
    }
    contentType_ = contentType;
}

void AsyncCallbackResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) return;
    if (!content_) {
        state_ = RESPONSE_FAILED;
        req->finishResponse();
        return;
    }

    state_ = RESPONSE_CONTENT;
    req_ = req;
    client_ = req->client();
    header_ = assembleHead(req->version());

    client_->Append(header_.c_str(), header_.length(),
                    TCP_WRITE_FLAG_COPY | TCP_WRITE_FLAG_MORE);

    client_->OnDrain([](void* ctx) {
        auto* self = static_cast<AsyncCallbackResponse*>(ctx);
        if (self->done_) {
            self->finish();
        } else {
            self->pump();
        }
    }, this);

    pump();
}

void AsyncCallbackResponse::pump()
{
    constexpr size_t kBatchPerDrain = 4;
    constexpr size_t kDataChunk      = 1460;

    for (size_t i = 0; i < kBatchPerDrain; ++i) {
        auto* buf = static_cast<uint8_t*>(malloc(kDataChunk));
        if (!buf) break;

        size_t n = content_(buf, kDataChunk, filledLength_);
        if (n == RESPONSE_TRY_AGAIN) { free(buf); break; }
        if (n == 0) {
            free(buf);
            done_ = true;
            break;
        }

        uint8_t flags = (i == kBatchPerDrain - 1) ? 0 : TCP_WRITE_FLAG_MORE;

        bool ok = client_->Append(buf, n, flags,
            [](void* ctx, size_t) { free(ctx); }, buf);
        if (!ok) { free(buf); break; }

        filledLength_ += n;
    }
    client_->Flush();
}