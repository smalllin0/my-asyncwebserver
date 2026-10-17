// AsyncCallbackResponse.cc
#include "AsyncCallbackResponse.h"
#include "../request/AsyncWebServerRequest.h"

AsyncCallbackResponse::AsyncCallbackResponse(const std::string& contentType, size_t len,
                                             AwsResponseFiller cb,
                                             AwsTemplateProcessor /*templateCb*/)
    : content_(cb)
{
    code_ = 200;
    contentLength_ = len;
    if (len == 0) {
        sendContentLength_ = false;
    }
    contentType_ = contentType;
}

AsyncCallbackResponse::~AsyncCallbackResponse()
{
    if (buffer_) { free(buffer_); buffer_ = nullptr; }
}

void AsyncCallbackResponse::pump()
{
    constexpr size_t kDataChunk = 1460;
    if (done_) return;

    // 首次分配，之后复用 —— 每次 append 后被 OnDrain 回调驱动，
    // 触发时上一块已发送完毕，buffer_ 可安全覆盖
    if (!buffer_) {
        buffer_ = static_cast<uint8_t*>(malloc(kDataChunk));
        if (!buffer_) return;
    }

    size_t n = content_(buffer_, kDataChunk, filledLength_);

    if (n == RESPONSE_TRY_AGAIN) {
        return;                              // 数据未就绪，下次 drain 重试
    }
    if (n == 0) {
        done_ = true;                        // 结束
        return;
    }

    bool isLast = (sendContentLength_ && contentLength_ &&
                   filledLength_ + n >= contentLength_);
    uint8_t flags = isLast ? 0 : TCP_WRITE_FLAG_MORE;

    if (!req_->append(buffer_, n, flags)) return;   // 提交失败，下轮重试
    filledLength_ += n;
    if (isLast) done_ = true;
}
