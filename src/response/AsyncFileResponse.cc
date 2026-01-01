#include "AsyncFileResponse.h"
#include "../tools.h"

AsyncFileResponse::AsyncFileResponse(std::string path, std::string contentType, bool download, AwsTemplateProcessor cb)
    : AsyncAbstractResponse(cb)
    , path_(std::move(path))
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
            callback_ = nullptr;
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
        value += path_.substr(path_.find_last_of('/') + 1);
        value += R"(")";
    } else {
        value += R"(inline; filename=")";
        value += path_.substr(path_.find_last_of('/') + 1);
        value += R"(")";
    }
    addHeader("Content-Disposition", std::move(value));
}

AsyncFileResponse::~AsyncFileResponse()
{
    auto fd = file_;
    file_ = nullptr;
    fd && fclose(fd);
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
        contentType_ = "text_plain";
        return;
    }

    
    switch (const_hash(path.substr(dot_index).c_str())) {
        case const_hash(".htm") :
        case const_hash(".html") :  contentType_ = "text/html";                 break;
        case const_hash(".js"):     contentType_ = "application/javascript";    break;
        case const_hash(".ico"):    contentType_ = "image/x-icon";              break;
        case const_hash(".css"):    contentType_ = "text/css";                  break;
        case const_hash(".json"):   contentType_ = "application/json";          break;
        case const_hash(".png"):    contentType_ = "image/png";                 break;
        case const_hash(".gif"):    contentType_ = "image/gif";                 break;
        case const_hash(".jpg"):
        case const_hash(".jpeg"):   contentType_ = "image/jpeg";                break;
        case const_hash(".svg"):    contentType_ = "image/svg+xml";             break;
        case const_hash(".eot"):    contentType_ = "font/eot";                  break;
        case const_hash(".woff"):   contentType_ = "font/woff";                 break;
        case const_hash(".woff2"):  contentType_ = "font/woff2";                break;
        case const_hash(".ttf"):    contentType_ = "font/ttf";                  break;
        case const_hash(".xml"):    contentType_ = "text/xml";                  break;
        case const_hash(".pdf"):    contentType_ = "application/pdf";           break;
        case const_hash(".zip"):    contentType_ = "application/zip";           break;
        case const_hash(".gz"):     contentType_ = "application/x-gzip";        break;
        default:                    contentType_ = "text/plain";                break;
    }
}