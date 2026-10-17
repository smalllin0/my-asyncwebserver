// AsyncChunkedResponse.cc
#include "AsyncChunkedResponse.h"
#include "../request/AsyncWebServerRequest.h"

AsyncChunkedResponse::AsyncChunkedResponse(const std::string& contentType,
                                           AwsResponseFiller cb,
                                           AwsTemplateProcessor /*templateCb*/)
    : content_(cb)
{
    code_ = 200;
    contentLength_ = 0;
    contentType_ = contentType;
    sendContentLength_ = false;
    chunked_ = true;
}

AsyncChunkedResponse::~AsyncChunkedResponse()
{
    if (buffer_) { free(buffer_); buffer_ = nullptr; }
}

void AsyncChunkedResponse::pump()
{
    constexpr size_t kDataChunk = 1024;
    if (done_) return;

    // 一次分配：帧头 6B（XXXX\r\n）+ payload + 帧尾 2B（\r\n）
    if (!buffer_) {
        buffer_ = static_cast<uint8_t*>(malloc(kDataChunk + 8));
        if (!buffer_) return;
    }

    // payload 写在偏移 6 处
    size_t n = content_(buffer_ + 6, kDataChunk, filledLength_);

    if (n == RESPONSE_TRY_AGAIN) {
        return;
    }
    if (n == 0) {
        // 生成器结束，写终止 chunk（静态字面量，无需 buffer_）
        static const char term[] = "0\r\n\r\n";
        req_->append(term, 5, TCP_WRITE_FLAG_MORE);
        done_ = true;
        return;
    }

    // 原地组帧
    static const char hex[] = "0123456789abcdef";
    buffer_[0] = hex[(n >> 12) & 0xF];
    buffer_[1] = hex[(n >>  8) & 0xF];
    buffer_[2] = hex[(n >>  4) & 0xF];
    buffer_[3] = hex[(n      ) & 0xF];
    buffer_[4] = '\r';
    buffer_[5] = '\n';
    buffer_[n + 6] = '\r';
    buffer_[n + 7] = '\n';

    if (!req_->append(buffer_, n + 8, TCP_WRITE_FLAG_MORE)) return;
    filledLength_ += n;
}
