// AsyncAbstractResponse.cc
#include "AsyncAbstractResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include <cstring>

AsyncAbstractResponse::AsyncAbstractResponse(AwsTemplateProcessor cb)
    : callback_(cb)
{
    if (cb) {
        contentLength_ = 0;
        sendContentLength_ = false;
        chunked_ = true;
    }
}

AsyncAbstractResponse::~AsyncAbstractResponse()
{
    if (buffer_) { free(buffer_); buffer_ = nullptr; }
}

void AsyncAbstractResponse::processTemplates(const std::vector<uint8_t>& raw,
                                             std::vector<uint8_t>& out)
{
    if (!callback_) {
        out = raw;
        return;
    }

    out.clear();
    out.reserve(raw.size());

    size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] != CONFIG_TEMPLATE_PLACEHOLDER) {
            out.push_back(raw[i++]);
            continue;
        }
        size_t end = i + 1;
        while (end < raw.size() && raw[end] != CONFIG_TEMPLATE_PLACEHOLDER) ++end;
        if (end >= raw.size()) {
            out.insert(out.end(), raw.begin() + i, raw.end());
            break;
        }
        if (end == i + 1) {
            out.push_back(CONFIG_TEMPLATE_PLACEHOLDER);
        } else {
            std::string name(reinterpret_cast<const char*>(raw.data() + i + 1), end - i - 1);
            std::string value = callback_(name);
            out.insert(out.end(), value.begin(), value.end());
        }
        i = end + 1;
    }
}

void AsyncAbstractResponse::pump()
{
    constexpr size_t kDataChunk = 1024;
    if (done_) return;

    if (!buffer_) {
        buffer_ = static_cast<uint8_t*>(malloc(kDataChunk));
        if (!buffer_) return;
    }

    size_t n = fillBuffer(buffer_, kDataChunk);
    if (n == 0) {
        if (chunked_) {
            static const char term[] = "0\r\n\r\n";
            req_->append(term, 5, TCP_WRITE_FLAG_MORE);
        }
        done_ = true;
        return;
    }

    std::vector<uint8_t> raw(buffer_, buffer_ + n);
    processTemplates(raw, outBuf_);

    if (chunked_) {
        // 帧头 6B + payload + 帧尾 2B，直接用 outBuf_ 外层包一层
        size_t payload = outBuf_.size();
        std::vector<uint8_t> framed(payload + 8);
        static const char hex[] = "0123456789abcdef";
        framed[0] = hex[(payload >> 12) & 0xF];
        framed[1] = hex[(payload >>  8) & 0xF];
        framed[2] = hex[(payload >>  4) & 0xF];
        framed[3] = hex[(payload      ) & 0xF];
        framed[4] = '\r';
        framed[5] = '\n';
        if (payload) memcpy(framed.data() + 6, outBuf_.data(), payload);
        framed[payload + 6] = '\r';
        framed[payload + 7] = '\n';
        if (!req_->append(framed.data(), framed.size(), TCP_WRITE_FLAG_MORE)) return;
    } else {
        if (outBuf_.empty()) return;
        if (!req_->append(outBuf_.data(), outBuf_.size(), TCP_WRITE_FLAG_MORE)) return;
    }
    sentLength_ += outBuf_.size();
}