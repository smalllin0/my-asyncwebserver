#include "AsyncChunkedResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"

AsyncChunkedResponse::AsyncChunkedResponse(const std::string &contentType,
                                           AwsResponseFiller cb,
                                           AwsTemplateProcessor templateCb)
    : content_(cb)
{
    code_ = 200;
    contentLength_ = 0;
    contentType_ = contentType;
    sendContentLength_ = false;
    chunked_ = true;
}

void AsyncChunkedResponse::respond(AsyncWebServerRequest* req)
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
        auto* self = static_cast<AsyncChunkedResponse*>(ctx);
        if (self->done_) {
            self->finish();
        } else {
            self->pump();
        }
    }, this);

    pump();
}

void AsyncChunkedResponse::pump()
{
    constexpr size_t kBatchPerDrain = 4;
    constexpr size_t kDataChunk      = 1024;

    for (size_t i = 0; i < kBatchPerDrain; ++i) {
        auto* raw = static_cast<uint8_t*>(malloc(kDataChunk + 8));
        if (!raw) break;

        size_t n = content_(raw + 6, kDataChunk, filledLength_);
        if (n == RESPONSE_TRY_AGAIN) { free(raw); break; }

        if (n == 0) {
            // 生成器结束，写终止 chunk "0\r\n\r\n"
            free(raw);
            auto* term = static_cast<uint8_t*>(malloc(5));
            memcpy(term, "0\r\n\r\n", 5);
            client_->Append(term, 5, TCP_WRITE_FLAG_MORE,
                [](void* ctx, size_t) { free(ctx); }, term);
            done_ = true;
            break;
        }

        // 组装 chunked 帧：XXXX\r\n<payload>\r\n
        sprintf(reinterpret_cast<char*>(raw), "%04x\r\n", static_cast<unsigned>(n));
        raw[n + 6] = '\r';
        raw[n + 7] = '\n';

        bool ok = client_->Append(raw, n + 8, TCP_WRITE_FLAG_MORE,
            [](void* ctx, size_t) { free(ctx); }, raw);
        if (!ok) { free(raw); break; }   // ★ 失败要 break

        filledLength_ += n;
    }
    client_->Flush();
}