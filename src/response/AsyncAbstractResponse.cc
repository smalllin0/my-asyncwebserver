#include "AsyncAbstractResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"
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

void AsyncAbstractResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) return;
    if (!sourceValid()) {
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
        auto* self = static_cast<AsyncAbstractResponse*>(ctx);
        if (self->done_) {
            self->finish();
        } else {
            self->pump();
        }
    }, this);

    pump();
}

size_t AsyncAbstractResponse::readDataFromCacheOrContent(uint8_t* data, size_t len)
{
    const size_t readFromCache = std::min(len, cache_.size());
    if (readFromCache) {
        memcpy(data, cache_.data(), readFromCache);
        cache_.erase(cache_.begin(), cache_.begin() + readFromCache);
    }
    const size_t needFromFile = len - readFromCache;
    const size_t readFromContent = fillBuffer(data + readFromCache, needFromFile);
    return readFromCache + readFromContent;
}

void AsyncAbstractResponse::processTemplates(const std::vector<uint8_t>& raw,
                                             std::vector<uint8_t>& out)
{
    // 简化版：逐块扫描 %name%，替换为 callback_(name)
    // 跨块边界的模板参数不支持（原版支持，但更复杂）
    // 如需完整支持，请参考原 fillBufferAndProcessTemplates 并适配到"输入→输出"模型
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
        // 找结束的 % 或连续两个 %%
        size_t end = i + 1;
        while (end < raw.size() && raw[end] != CONFIG_TEMPLATE_PLACEHOLDER) {
            ++end;
        }
        if (end >= raw.size()) {
            // 未闭合，原样输出剩余
            out.insert(out.end(), raw.begin() + i, raw.end());
            break;
        }
        // raw[i] 是起始 %，raw[end] 是结束 %
        if (end == i + 1) {
            // 连续两个 %%，转义为单个 %
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
    constexpr size_t kBatchPerDrain = 2;    // 模板处理慢，批小
    constexpr size_t kDataChunk      = 1024;

    for (size_t i = 0; i < kBatchPerDrain; ++i) {
        std::vector<uint8_t> raw(kDataChunk);
        size_t n = readDataFromCacheOrContent(raw.data(), kDataChunk);
        if (n == 0) {
            done_ = true;
            break;
        }
        raw.resize(n);

        std::vector<uint8_t> out;
        processTemplates(raw, out);

        if (chunked_) {
            std::vector<uint8_t> framed(out.size() + 8);
            sprintf(reinterpret_cast<char*>(framed.data()), "%04x\r\n",
                    static_cast<unsigned>(out.size()));
            framed[5] = '\n';
            memcpy(framed.data() + 6, out.data(), out.size());
            framed[out.size() + 6] = '\r';
            framed[out.size() + 7] = '\n';
            out = std::move(framed);
        }

        auto* buf = static_cast<uint8_t*>(malloc(out.size()));
        if (!buf) break;
        memcpy(buf, out.data(), out.size());

        bool ok = client_->Append(buf, out.size(), TCP_WRITE_FLAG_MORE,
            [](void* ctx, size_t) { free(ctx); }, buf);
        if (!ok) { free(buf); break; }

        sentLength_ += out.size();
    }

    if (done_ && chunked_) {
        auto* term = static_cast<uint8_t*>(malloc(5));
        memcpy(term, "0\r\n\r\n", 5);
        client_->Append(term, 5, TCP_WRITE_FLAG_MORE,
            [](void* ctx, size_t) { free(ctx); }, term);
    }

    client_->Flush();
}