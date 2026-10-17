// AsyncFileResponse.cc
#include "AsyncFileResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "../tools.h"
#include <sys/stat.h>

AsyncFileResponse::AsyncFileResponse(std::string path, std::string contentType,
                                     bool download, AwsTemplateProcessor /*cb*/)
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

    struct stat st{};
    stat(path_.c_str(), &st);
    contentLength_ = st.st_size;
    file_ = fopen(path_.c_str(), "r");

    std::string value;
    value.reserve(128);
    value += download ? R"(attachment; filename=")" : R"(inline; filename=")";
    value += path_.substr(path_.find_last_of('/') + 1);
    value += R"(")";
    addHeader("Content-Disposition", std::move(value));
}

AsyncFileResponse::~AsyncFileResponse()
{
    if (file_) { fclose(file_); file_ = nullptr; }
    if (buffer_) { free(buffer_); buffer_ = nullptr; }
}

constexpr uint32_t const_hash(const char* str) {
    uint32_t hash = 5381;
    while (*str) { hash = ((hash << 5) + hash) + *str; str++; }
    return hash;
}

void AsyncFileResponse::setContentType(const std::string& path)
{
    size_t dot_index = path.find_last_of('.');
    if (dot_index == std::string::npos) { contentType_ = "text/plain"; return; }
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

void AsyncFileResponse::pump()
{
    constexpr size_t kChunkSize = 1460;
    if (done_) return;

    if (readLength_ >= contentLength_) { done_ = true; return; }

    if (!buffer_) {
        buffer_ = static_cast<uint8_t*>(malloc(kChunkSize));
        if (!buffer_) return;
    }

    size_t toRead = std::min(kChunkSize, contentLength_ - readLength_);
    size_t n = fread(buffer_, 1, toRead, file_);
    if (n == 0) { done_ = true; return; }

    bool isLast = (readLength_ + n >= contentLength_);
    uint8_t flags = isLast ? 0 : TCP_WRITE_FLAG_MORE;

    if (!req_->append(buffer_, n, flags)) return;   // 提交失败，下轮重试
    readLength_ += n;
    if (isLast) done_ = true;
}

