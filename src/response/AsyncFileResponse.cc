#include "AsyncFileResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncConnection.h"
#include "../tools.h"

AsyncFileResponse::AsyncFileResponse(std::string path, std::string contentType,
                                     bool download, AwsTemplateProcessor cb)
    : path_(std::move(path))
{
    code_ = 200;

    if (contentType.empty()) {
        setContentType(path_);
    } else {
        contentType_ = std::move(contentType);
    }

    if (!download && !FILE_EXISTS(path_.c_str())) {
        path_ += ".gz";
        if (FILE_EXISTS(path_.c_str())) {
            addHeader("Content-Encoding", "gzip");
            sendContentLength_ = true;
            chunked_ = false;
        }
    }

    struct stat st;
    stat(path_.c_str(), &st);
    contentLength_ = st.st_size;
    file_ = fopen(path_.c_str(), "r");

    std::string value;
    value.reserve(128);
    if (download) {
        value += R"(attachment; filename=")";
    } else {
        value += R"(inline; filename=")";
    }
    value += path_.substr(path_.find_last_of('/') + 1);
    value += R"(")";
    addHeader("Content-Disposition", std::move(value));
}

AsyncFileResponse::~AsyncFileResponse()
{
    auto fd = file_;
    file_ = nullptr;
    if (fd) fclose(fd);
}

constexpr uint32_t const_hash(const char* str) {
    uint32_t hash = 5381;
    while (*str) {
        hash = ((hash << 5) + hash) + *str;
        str++;
    }
    return hash;
}

void AsyncFileResponse::setContentType(const std::string& path)
{
    size_t dot_index = path.find_last_of('.');
    if (dot_index == std::string::npos) {
        contentType_ = "text/plain";
        return;
    }
    switch (const_hash(path.substr(dot_index).c_str())) {
        case const_hash(".htm"):
        case const_hash(".html"):  contentType_ = "text/html";              break;
        case const_hash(".js"):    contentType_ = "application/javascript"; break;
        case const_hash(".ico"):   contentType_ = "image/x-icon";           break;
        case const_hash(".css"):   contentType_ = "text/css";               break;
        case const_hash(".json"):  contentType_ = "application/json";       break;
        case const_hash(".png"):   contentType_ = "image/png";              break;
        case const_hash(".gif"):   contentType_ = "image/gif";              break;
        case const_hash(".jpg"):
        case const_hash(".jpeg"):  contentType_ = "image/jpeg";             break;
        case const_hash(".svg"):   contentType_ = "image/svg+xml";          break;
        case const_hash(".eot"):   contentType_ = "font/eot";               break;
        case const_hash(".woff"):  contentType_ = "font/woff";              break;
        case const_hash(".woff2"): contentType_ = "font/woff2";             break;
        case const_hash(".ttf"):   contentType_ = "font/ttf";               break;
        case const_hash(".xml"):   contentType_ = "text/xml";               break;
        case const_hash(".pdf"):   contentType_ = "application/pdf";        break;
        case const_hash(".zip"):   contentType_ = "application/zip";        break;
        case const_hash(".gz"):    contentType_ = "application/x-gzip";     break;
        default:                   contentType_ = "text/plain";             break;
    }
}

void AsyncFileResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) return;
    if (!file_) {
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
        auto* self = static_cast<AsyncFileResponse*>(ctx);
        if (self->readLength_ >= self->contentLength_) {
            self->finish();
        } else {
            self->pump();
        }
    }, this);

    pump();
}

void AsyncFileResponse::pump()
{
    constexpr size_t kBatchPerDrain = 4;
    constexpr size_t kChunkSize      = 1460;

    for (size_t i = 0; i < kBatchPerDrain; ++i) {
        if (readLength_ >= contentLength_) break;

        size_t toRead = std::min(kChunkSize, contentLength_ - readLength_);
        auto* buf = static_cast<uint8_t*>(malloc(toRead));
        if (!buf) break;

        size_t n = fread(buf, 1, toRead, file_);
        if (n == 0) { free(buf); break; }

        // 本批最后一笔不带 MORE
        uint8_t flags = (i == kBatchPerDrain - 1) ? 0 : TCP_WRITE_FLAG_MORE;

        bool ok = client_->Append(buf, n, flags,
            [](void* ctx, size_t) { free(ctx); }, buf);
        if (!ok) { free(buf); break; }

        readLength_ += n;
    }
    client_->Flush();
}

