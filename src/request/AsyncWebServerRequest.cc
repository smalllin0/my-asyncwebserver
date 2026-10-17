
// =============================================================
// 文件：AsyncWebServerRequest.cc
// =============================================================
#include "RequestPool.h"
#include "AsyncWebServerRequest.h"
#include <esp_log.h>
#include "AsyncWebServer.h"
#include "../header/AsyncWebHeader.h"
#include "../StringArray.h"
#include "../parameter/AsyncWebParameter.h"
#include "../response/AsyncWebServerResponse.h"
#include "../response/AsyncBasicResponse.h"
#include "../response/AsyncChunkedResponse.h"
#include "../response/AsyncCallbackResponse.h"
#include "../response/AsyncProgmemResponse.h"
#include "../response/AsyncFileResponse.h"
#include "../handler/AsyncWebHandler.h"
#include "../WebAuthentication.h"
#include "../tools.h"

#define TAG "AsyncWebServerRequest"
#define __is_param_char(c) ((c) && ((c)!='{') && ((c)!='[') && ((c)!='&') && ((c)!='='))

enum {
    PARSE_REQ_START,
    PARSE_REQ_HEADERS,
    PARSE_REQ_BODY,
    PARSE_REQ_END,
    PARSE_REQ_FAIL
};

void AsyncWebServerRequest::reset()
{
    headers_.free();
    params_.free();
    pathParams_.free();
    interestingHeaders_.free();

    auto* response = response_;
    response_ = nullptr;
    if (response) {
        delete response;
    }

    auto* tmp = fileName_;
    fileName_ = nullptr;
    if (tmp) {
        delete tmp;
    }
}

AsyncWebServerRequest::AsyncWebServerRequest()
    : headers_(LinkedList<AsyncWebHeader*>([](AsyncWebHeader* header){ delete header; }))
    , params_(LinkedList<AsyncWebParameter*>([](AsyncWebParameter* param){ delete param; }))
    , pathParams_(LinkedList<std::string*>([](std::string* path){ delete path; }))
{ }

void AsyncWebServerRequest::init(AsyncWebServer* server, AsyncConnection* conn)
{
    conn_     = conn;
    server_     = server;
    handler_    = nullptr;
    response_   = nullptr;
    interestingHeaders_.free();
    onDisconnectfn_ = nullptr;

    tmp_            = empty_string;
    isFragmented_   = false;
    parseState_     = PARSE_REQ_START;
    version_        = 1;
    method_         = HTTP_ANY;
    url_            = empty_string;
    host_           = empty_string;
    contentType_    = empty_string;
    boundary_       = empty_string;
    authorization_  = empty_string;
    reqconntype_    = RCT_HTTP;

    isDigest_           = false;
    isMultipart_        = false;
    isPlainPost_        = false;
    expectingContinue_  = false;
    contentLength_      = 0;
    parsedLength_       = 0;
    boundaryMatchLen_   = 0;

    multiParseState_    = 0;
    boundaryPosition_   = 0;
    itemIsFile_         = false;
    itemStartIndex_     = 0;
    itemSize_           = 0;
    itemBufferIndex_    = 0;
    itemBuffer_         = nullptr;
    itemName_           = empty_string;
    itemFileName_       = empty_string;
    itemType_           = empty_string;
    itemValue_          = empty_string;

    // 数据到达
    conn->OnData([](void* ctx, void* data, size_t len) {
        auto* req = reinterpret_cast<AsyncWebServerRequest*>(ctx);
        req->onData((void*)data, len);
    }, this);

    // 错误
    conn->OnError([](void* r, int8_t err) {
        auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
        req->onErr(err);
    }, this);

    // ★ 删除 OnSent 注册 —— 新模型下响应推进由 OnDrain 驱动

    // 周期轮询
    conn->OnPoll([](void* r) {
        auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
        req->onPoll();
    }, this);

    // 回收前清理
    conn->OnRelease([](void* arg) {
        auto* req = (AsyncWebServerRequest*)arg;
        req->onDisconnect();
        req->reset();
        RequestPool::GetInstance().Release(req);
    }, this);
}

void AsyncWebServerRequest::onErr(err_t error)
{
    ESP_LOGE(TAG, "Request err, err code=%d", error);
}

/// @brief 断开回调
void AsyncWebServerRequest::onDisconnect()
{
    if (onDisconnectfn_ != nullptr) {
        onDisconnectfn_();
    }
    server_->internalHandleDisconnect(this);
}

/// @brief 周期轮询
/// @note 新模型下 response 的推进由 AsyncConnection::OnDrain 驱动
///       这里不再需要主动调用 response_->ack()
void AsyncWebServerRequest::onPoll()
{
    // 空实现，保留钩子供将来扩展
}

/// @brief 数据到达处理（接近零拷贝，仅在分片时缓存）
void AsyncWebServerRequest::onData(void* buf, size_t len)
{
    while (true) {
        if (parseState_ < PARSE_REQ_BODY) {
            auto* str = (char*)buf;
            size_t i = 0;
            while (i < len && str[i] != '\n') { i++; }
            if (i >= len) {
                tmp_.append(str, len);
                isFragmented_ = true;
            } else {
                if (isFragmented_) {
                    tmp_.append(str, i);
                    parseLine(&tmp_[0], &tmp_[tmp_.length()]);
                    isFragmented_ = false;
                    tmp_.clear();
                } else {
                    parseLine(str, str + i + 1);
                }
                if (++i < len) {
                    buf = str + i;
                    len -= i;
                    continue;
                }
            }
        } else if (parseState_ == PARSE_REQ_BODY) {
            const bool needParse = (handler_ && !(handler_->isRequestHandlerTrivial()));
            if (isMultipart_) {
                if (needParse) {
                    handleMultipartBody(buf, len);
                }
            } else {
                if (parsedLength_ == 0) {
                    if (contentType_.starts_with("application/x-www-form-urlencoded")) {
                        isPlainPost_ = true;
                    } else if (contentType_ == "text/plain" && __is_param_char(((char*)buf)[0])) {
                        size_t index = 0;
                        while (index < len && __is_param_char(((char*)buf)[index])) { index++; }
                        if (index < len && ((char*)buf)[index - 1] == '=') {
                            isPlainPost_ = true;
                        }
                    }
                }
                if (isPlainPost_) {
                    if (needParse) {
                        parsePlainPost((uint8_t*)buf, len);
                    }
                } else {
                    if (handler_) {
                        handler_->handleBody(this, (uint8_t*)buf, len,
                                             parsedLength_, contentLength_);
                    }
                }
            }
            parsedLength_ += len;

            if (parsedLength_ >= contentLength_) {
                parseState_ = PARSE_REQ_END;
                if (handler_) {
                    handler_->handleRequest(this);
                } else {
                    send(501);
                }
            }
        }
        break;
    }
}

/// @brief 解析每行数据
void AsyncWebServerRequest::parseLine(char* start, char* end)
{
    while (start < end && std::isspace(static_cast<unsigned char>(*start))) { start++; }
    if (start < end) {
        while (end > start && std::isspace(static_cast<unsigned char>(*(--end))));
    }
    auto is_empty_line = (start == end);
    end++;

    if (parseState_ == PARSE_REQ_START) {
        if (is_empty_line) {
            parseState_ = PARSE_REQ_FAIL;
            conn_->Close();
            ESP_LOGE(TAG, "解析失败: 请求行为空.");
        } else {
            parseReqLine(start, end);
            parseState_ = PARSE_REQ_HEADERS;
        }
        return;
    }

    if (parseState_ == PARSE_REQ_HEADERS) {
        if (!is_empty_line) {
            parseReqHeader(start, end);
        } else {
            server_->internalRewriteRequest(this);
            server_->internalAttachHandler(this);
            removeNotInterestingHeaders();

            if (expectingContinue_) {
                static const char* response = "HTTP/1.1 100 Continue\r\n\r\n";
                conn_->Append((void*)response, strlen(response), TCP_WRITE_FLAG_MORE);
                conn_->Flush();
            }
            if (contentLength_) {
                parseState_ = PARSE_REQ_BODY;
            } else {
                parseState_ = PARSE_REQ_END;
                if (handler_) {
                    handler_->handleRequest(this);
                } else {
                    send(501);
                }
            }
        }
    }
}

/// @brief 解析简单表单
void AsyncWebServerRequest::parsePlainPost(uint8_t* data, size_t len)
{
    size_t pos = 0;
    uint8_t* start;
    uint8_t* end;
    uint8_t* equal_ptr;
    bool isFinal = (parsedLength_ + len == contentLength_);

    while (pos < len) {
        size_t step = 0;
        while (pos + step < len && data[pos + step] != '\0'
               && (char)(data[pos + step]) != '&') {
            step++;
        }
        if (pos + step == len && !isFinal) {
            tmp_.assign((const char*)(data + pos), step);
            parsedLength_ += len;
            return;
        } else {
            if (data[pos + step] == '\0' || (char)(data[pos + step]) == '&' || isFinal) {
                bool isParseTmp = false;
                if (tmp_.empty()) {
                    start = data + pos;
                    end = start + step;
                } else {
                    tmp_.append((const char*)(data + pos), step);
                    start = (uint8_t*)tmp_.c_str();
                    end = start + tmp_.length();
                    isParseTmp = true;
                }
                equal_ptr = start;
                while (equal_ptr < end && *equal_ptr != '=') { equal_ptr++; }

                if (*start != '{' && *start != '[' && equal_ptr != end) {
                    params_.add(new AsyncWebParameter(
                        urlDecode((const char*)start, (const char*)equal_ptr),
                        urlDecode((const char*)equal_ptr + 1, (const char*)end),
                        true
                    ));
                }
                if (isParseTmp) { tmp_.clear(); }
            }
        }
        pos += step + 1;
    }
    parsedLength_ += len;
}

/// @brief 解析 HTTP 请求行
void AsyncWebServerRequest::parseReqLine(char* start, char* end)
{
    auto* space1 = start;
    while (space1 < end && *space1 != ' ') space1++;
    auto* space2 = space1 + 1;
    while (space2 < end && *space2 != ' ') space2++;

    uint8_t method_len = space1 - start;
    auto hash = (start[0]<<16) | (method_len < 2 ? 0 : (start[1]<<8)) | method_len;
    switch (hash) {
        case ('G'<<16) | ('E'<<8) | 3 :  method_ = HTTP_GET;     break;
        case ('P'<<16) | ('O'<<8) | 4 :  method_ = HTTP_POST;    break;
        case ('D'<<16) | ('E'<<8) | 6 :  method_ = HTTP_DELETE;  break;
        case ('P'<<16) | ('U'<<8) | 3 :  method_ = HTTP_PUT;     break;
        case ('P'<<16) | ('A'<<8) | 5 :  method_ = HTTP_PATCH;   break;
        case ('H'<<16) | ('E'<<8) | 4 :  method_ = HTTP_HEAD;    break;
        case ('O'<<16) | ('P'<<8) | 7 :  method_ = HTTP_OPTIONS; break;
        default: method_ = HTTP_ANY;
    }

    auto* url_start = space1 + 1;
    auto* question = url_start;
    while (question < space2 && *question != '?') question++;
    if (question < space2) {
        addGetParams(question + 1, space2);
        url_.assign(url_start, question - url_start);
    } else {
        url_.assign(url_start, space2 - url_start);
    }

    if (memcmp((void*)(space2 + 1), "HTTP/1.1", 8) == 0) {
        version_ = 1;
    }
}

void AsyncWebServerRequest::addGetParams(const char* start, const char* end)
{
    while (start < end) {
        auto* equal_ptr = start;
        while (equal_ptr < end && *equal_ptr != '=') equal_ptr++;
        auto* ampersan_ptr = equal_ptr + 1;
        while (ampersan_ptr < end && *ampersan_ptr != '&') ampersan_ptr++;

        if (equal_ptr >= end - 1) {
            params_.add(new AsyncWebParameter(urlDecode(start, equal_ptr), ""));
        } else {
            std::string value;
            if (equal_ptr < end && ampersan_ptr > equal_ptr - 2) {
                value = urlDecode(equal_ptr + 1, ampersan_ptr);
            }
            params_.add(new AsyncWebParameter(urlDecode(start, equal_ptr), std::move(value)));
        }
        start = ampersan_ptr + 1;
    }
}

std::string AsyncWebServerRequest::urlDecode(const char* start, const char* end) const
{
    char tmp[] = "0x00";
    char decoded;
    std::string decoded_str;
    decoded_str.reserve(end - start);

    while (start < end) {
        if ((*start == '%') && (start + 2 < end)) {
            tmp[2] = *(start++);
            tmp[3] = *(start++);
            decoded = strtol(tmp, nullptr, 16);
        } else if (*start == '+') {
            decoded = ' ';
            start++;
        } else {
            decoded = *start;
            start++;
        }
        decoded_str.push_back(decoded);
    }
    return decoded_str;
}

enum HeaderID : uint32_t {
    kAccept         = ('a'<<24) | ('c'<<16) | ('c'<<8) | 6,
    kAcceptCharset  = ('a'<<24) | ('c'<<16) | ('c'<<8) | 14,
    kAcceptEncoding = ('a'<<24) | ('c'<<16) | ('c'<<8) | 15,
    kAcceptLanguage = ('a'<<24) | ('c'<<16) | ('c'<<8) | 16,
    kAuthorization  = ('a'<<24) | ('u'<<16) | ('t'<<8) | 13,
    kCacheControl   = ('c'<<24) | ('a'<<16) | ('c'<<8) | 13,
    kConnection     = ('c'<<24) | ('o'<<16) | ('n'<<8) | 10,
    kCookie         = ('c'<<24) | ('o'<<16) | ('o'<<8) | 6,
    kDate           = ('d'<<24) | ('a'<<16) | ('t'<<8) | 4,
    kExpect         = ('e'<<24) | ('x'<<16) | ('p'<<8) | 6,
    kForwarded      = ('f'<<24) | ('o'<<16) | ('r'<<8) | 9,
    kFrom           = ('f'<<24) | ('r'<<16) | ('o'<<8) | 4,
    kHost           = ('h'<<24) | ('o'<<16) | ('s'<<8) | 4,
    kIfMatch_Range  = ('i'<<24) | ('f'<<16) | ('-'<<8) | 8,
    kIfModifySince  = ('i'<<24) | ('f'<<16) | ('-'<<8) | 17,
    kIfNoneMatch    = ('i'<<24) | ('f'<<16) | ('-'<<8) | 13,
    kIfUnmodifySince= ('i'<<24) | ('f'<<16) | ('-'<<8) | 19,
    kMaxForwards    = ('m'<<24) | ('a'<<16) | ('x'<<8) | 12,
    kOrigin         = ('o'<<24) | ('r'<<16) | ('i'<<8) | 6,
    kPragma         = ('p'<<24) | ('r'<<16) | ('a'<<8) | 6,
    kProxyAuth      = ('p'<<24) | ('r'<<16) | ('o'<<8) | 20,
    kReferer        = ('r'<<24) | ('e'<<16) | ('f'<<8) | 7,
    kTE             = ('t'<<24) | ('e'<<16) | (0  <<8) | 2,
    kUserAgent      = ('u'<<24) | ('s'<<16) | ('e'<<8) | 10,
    kUpgrade        = ('u'<<24) | ('p'<<16) | ('g'<<8) | 7,
    kVia            = ('v'<<24) | ('i'<<16) | ('a'<<8) | 3,
    kWarning        = ('w'<<24) | ('a'<<16) | ('r'<<8) | 7,
    kContentMD5     = ('c'<<24) | ('o'<<16) | ('n'<<8) | 11,
    kContentType    = ('c'<<24) | ('o'<<16) | ('n'<<8) | 12,
    kContentRange   = ('c'<<24) | ('o'<<16) | ('n'<<8) | 13,
    kContentLength  = ('c'<<24) | ('o'<<16) | ('n'<<8) | 14,
    kCtnEncode_Locate_Language  = ('c'<<24) | ('o'<<16) | ('n'<<8) | 16,
    kDNT            = ('d'<<24) | ('n'<<16) | ('t'<<8) | 3,
    kUpgradeInsecureReq = ('u'<<24) | ('p'<<16) | ('g'<<8) | 25,
    kACLReqMethod   = ('a'<<24) | ('c'<<16) | ('c'<<8) | 24,
    kACLReqHeaders  = ('a'<<24) | ('c'<<16) | ('c'<<8) | 27,
    kSecFetch       = ('s'<<24) | ('e'<<16) | ('c'<<8) | 14,
    kXForwardedFor  = ('x'<<24) | ('-'<<16) | ('f'<<8) | 16,
    kXForwardedHost = ('x'<<24) | ('-'<<16) | ('f'<<8) | 17,
    kXForwardedProto= ('x'<<24) | ('-'<<16) | ('f'<<8) | 18,
    kXRealIP        = ('x'<<24) | ('-'<<16) | ('r'<<8) | 10,
    kXRequestedWith = ('x'<<24) | ('-'<<16) | ('r'<<8) | 18,
    kXCSRFToken     = ('x'<<24) | ('-'<<16) | ('c'<<8) | 12,
    kXXSRFToken     = ('x'<<24) | ('-'<<16) | ('x'<<8) | 12,
    kXAPIKey        = ('x'<<24) | ('-'<<16) | ('a'<<8) | 9,
    kUnknown        = 0
};

bool AsyncWebServerRequest::parseReqHeader(const char* start, const char* end)
{
    const auto* colon = start;
    while (colon < end && *colon != ':') colon++;
    auto value_start = colon + 1;
    while ((value_start < end) && (std::isspace(*value_start))) value_start++;

    if (end - value_start > 1) {
        size_t name_len = colon - start;
        if (name_len > 63) return false;
        uint32_t hash_code = name_len;
        hash_code |= (name_len > 2 ? tolower(start[2]) : '\0') << 8;
        hash_code |= (name_len > 1 ? tolower(start[1]) : '\0') << 16;
        hash_code |= tolower(start[0]) << 24;

        std::string name(start, name_len);
        std::string value(value_start, end - value_start);
        switch (hash_code) {
          case kHost:         host_ = value; break;
          case kContentType:
            contentType_ = value.substr(0, value.find(';'));
            if (value.starts_with("multipart")) {
                boundary_ = value.substr(value.find('=') + 1);
                boundary_.erase(std::remove(boundary_.begin(), boundary_.end(), '"'), boundary_.end());
                boundary_ += "--";
                isMultipart_ = true;
            }
            break;
          case kContentLength: contentLength_ = atoi(value.c_str()); break;
          case kExpect:
            if (value == "100-continue") expectingContinue_ = true;
            break;
          case kAuthorization:
            if (value.length() > 5 && (0 == strncasecmp(value.c_str(), "Basic", 5))) {
                authorization_ = value.substr(6);
            } else if (value.length() > 6 && (0 == strncasecmp(value.c_str(), "Digest", 6))) {
                isDigest_ = true;
                authorization_ = value.substr(7);
            }
            break;
          case kUpgrade:
            if (0 == strcasecmp(value.c_str(), "websocket")) reqconntype_ = RCT_WS;
            break;
          case kAccept:
            if (strContains(value, "text/event-stream", false)) reqconntype_ = RCT_EVENT;
            break;
        }
        headers_.add(new AsyncWebHeader(std::move(name), std::move(value)));
    }
    return true;
}

void AsyncWebServerRequest::removeNotInterestingHeaders()
{
    if (interestingHeaders_.containsIgnoreCase("ANY")) return;
    for (const auto &header : headers_) {
        if (!interestingHeaders_.containsIgnoreCase(header->name().c_str())) {
            headers_.remove(header);
        }
    }
}

enum MultipartState {
    MP_START,
    MP_MATCHING_BOUNDARY,
    MP_READING_HEADERS,
    MP_READING_BODY,
    MP_FINISHED,
    MP_ERROR
};

void AsyncWebServerRequest::handleMultipartBody(void* buf, size_t len)
{
    uint8_t* data = static_cast<uint8_t*>(buf);

    if (multiParseState_ >= MP_FINISHED) {
        multiParseState_ = MP_START;
        isFragmented_ = false;
        itemSize_ = 0;
        tmp_.clear();
        itemName_.clear();
        itemFileName_.clear();
        itemType_.clear();
    }

    size_t pos = 0;
handle_header_again:
    while (pos < len && multiParseState_ != MP_READING_BODY) {
        size_t nl = pos;
        while (nl < len && data[nl] != '\n') nl++;

        if (nl >= len) {
            if (!isFragmented_) {
                tmp_.clear();
                isFragmented_ = true;
            }
            tmp_.append(reinterpret_cast<const char*>(data + pos), len - pos);
            return;
        }

        uint8_t* line_start;
        uint8_t* line_end;

        if (isFragmented_) {
            tmp_.append(reinterpret_cast<const char*>(data + pos), nl - pos);
            if (!tmp_.empty() && tmp_.back() == '\r') {
                tmp_.pop_back();
            }
            if (tmp_.empty()) {
                multiParseState_ = MP_READING_BODY;
                pos += nl + 1;
                boundaryMatchLen_ = 0;
                break;
            } else {
                line_start = reinterpret_cast<uint8_t*>(tmp_.data());
                line_end = line_start + tmp_.size();
                parseMultiPartLine(line_start, line_end);
                isFragmented_ = false;
                tmp_.clear();
            }
        } else {
            line_start = data + pos;
            line_end = data + nl;
            if (nl > pos && data[nl - 1] == '\r') line_end--;
            if (line_start == line_end) {
                multiParseState_ = MP_READING_BODY;
                pos += nl + 1;
                boundaryMatchLen_ = 0;
                break;
            } else {
                parseMultiPartLine(line_start, line_end);
            }
        }
        pos = nl + 1;
    }

    if (multiParseState_ == MP_READING_BODY) {
        auto boundary_len = boundary_.length();
        while (pos < len && multiParseState_ == MP_READING_BODY) {
            size_t nl = pos;
            while (nl < len && data[nl] != boundary_[boundaryMatchLen_]) { nl++; }

            if (nl >= len) {
                handleUpload(data + pos, len, false);
                return;
            } else {
                auto match_len = std::min(len - nl, boundary_len);
                int i = 0;
                for (; i < match_len; i++) {
                    if (data[nl + i] != boundary_[boundaryMatchLen_++]) break;
                }
                if (boundaryMatchLen_ >= boundary_len) {
                    multiParseState_ = boundaryMatchLen_ == boundary_len ? MP_FINISHED : MP_READING_HEADERS;
                    if (multiParseState_ == MP_READING_HEADERS) {
                        pos += nl;
                        handleUpload(data + pos, pos + nl - 1, true);
                        goto handle_header_again;
                    } else {
                        handleUpload(data + pos, pos + nl - 1, false);
                        return;
                    }
                }
                if (pos + i == len) {
                    handleUpload(data + pos, len, false);
                    return;
                } else {
                    nl += i;
                    boundaryMatchLen_ = 0;
                }
            }
            pos += nl;
        }
    }
}

void AsyncWebServerRequest::parseMultiPartLine(uint8_t* start, uint8_t* end)
{
    switch (multiParseState_) {
      case MP_START:
        multiParseState_ = memcmp((void*)start, boundary_.c_str(), end - start) == 0
                           ? MP_READING_HEADERS : MP_ERROR;
        break;
      case MP_READING_HEADERS:
        size_t line_len = end - start;
        if (tmp_.empty()) {
            tmp_.assign((char*)start, end - start);
        }
        if (line_len > 19 && (0 == strncasecmp(tmp_.c_str(), "Content-Disposition", 19))) {
            tmp_ = tmp_.substr(tmp_.find(';') + 2);
            do {
                auto equal_index = tmp_.find('=');
                auto pos2 = tmp_.find(';');
                auto name = tmp_.substr(0, equal_index);
                if (name == "name") {
                    itemName_ = tmp_.substr(equal_index + 2, pos2 - (equal_index + 2));
                } else if (name == "filename") {
                    itemIsFile_ = true;
                    itemFileName_ = tmp_.substr(equal_index + 2, pos2 - (equal_index + 2));
                }
                tmp_ = tmp_.substr(pos2 + 2);
            } while (tmp_.find(';') != std::string::npos);
        } else if (line_len > 12 && (0 == strncasecmp(tmp_.c_str(), "Content-Type", 12))) {
            itemType_ = tmp_.substr(12 + 2);
            itemIsFile_ = true;
        }
        tmp_.clear();
        break;
    }
}

void AsyncWebServerRequest::handleUpload(uint8_t* data, size_t len, bool last)
{
    if (itemIsFile_) {
        if (handler_) {
            handler_->handleUpload(this, itemFileName_, itemSize_, data, len, last);
            itemSize_ += len;
        }
    } else {
        itemValue_.append((const char*)data, len);
    }
}

const char* AsyncWebServerRequest::methodToString() const
{
    switch (method_) {
      case HTTP_GET:     return "GET";
      case HTTP_POST:    return "POST";
      case HTTP_DELETE:  return "DELETE";
      case HTTP_PUT:     return "PUT";
      case HTTP_PATCH:   return "PATCH";
      case HTTP_HEAD:    return "HEAD";
      case HTTP_OPTIONS: return "OPTIONS";
      default:           return (method_ == HTTP_ANY) ? "ANY" : "UNKNOWN";
    }
}

const char* AsyncWebServerRequest::requestedConnTypeToString() const
{
    switch (reqconntype_) {
      case RCT_NOT_USED: return "RCT_NOT_USED";
      case RCT_DEFAULT:  return "RCT_DEFAULT";
      case RCT_HTTP:     return "RCT_HTTP";
      case RCT_WS:       return "RCT_WS";
      case RCT_EVENT:    return "RCT_EVENT";
      default:           return "ERROR";
    }
}

bool AsyncWebServerRequest::isExpectedRequestedConnType(RequestedConnectionType erct1,
                                                       RequestedConnectionType erct2,
                                                       RequestedConnectionType erct3)
{
    bool res = false;
    if ((erct1 != RCT_NOT_USED) && (erct1 == reqconntype_)) res = true;
    if ((erct2 != RCT_NOT_USED) && (erct2 == reqconntype_)) res = true;
    if ((erct3 != RCT_NOT_USED) && (erct3 == reqconntype_)) res = true;
    return res;
}

bool AsyncWebServerRequest::authenticate(const char * hash)
{
    if (hash == nullptr || authorization_.empty()) return false;

    if (isDigest_) {
        size_t pos1 = 0;
        size_t pos2 = 0;
        auto str_len = strlen(hash);
        while (pos1 < str_len && hash[pos1] != ':') pos1++;
        if (pos1 == str_len || pos1 == str_len - 1) {
            return false;
        } else {
            pos2 = pos1 + 1;
        }
        while (pos2 < str_len && hash[pos2] != ':') pos2++;
        if (pos2 == str_len) return false;

        std::string name;
        std::string realm;
        const char* hash_str = nullptr;

        name.assign(hash, pos1);
        realm.assign(hash + pos1 + 1, pos2 - pos1 - 1);
        hash_str = hash + pos2 + 1;

        return checkDigestAuthentication(authorization_.c_str(),
            methodToString(), name.c_str(), hash_str, realm.c_str(),
            true, nullptr, nullptr, nullptr);
    }

    return authorization_ == hash;
}

bool AsyncWebServerRequest::authenticate(const char* username, const char* password,
                                         const char* realm, bool passwordIsHash)
{
    if (authorization_.length()) {
        if (isDigest_) {
            return checkDigestAuthentication(authorization_.c_str(),
                methodToString(), username, password, realm,
                passwordIsHash, nullptr, nullptr, nullptr);
        } else if (!passwordIsHash) {
            return checkBasicAuthentication(authorization_.c_str(), username, password);
        } else {
            return authorization_ == password;
        }
    }
    return false;
}

void AsyncWebServerRequest::requestAuthentication(const char* realm, bool isDigest)
{
    AsyncWebServerResponse* r = beginResponse(401);
    if (!isDigest) {
        if (realm == nullptr) {
            r->addHeader("WWW-Authenticate", R"(Basic realm="Login Required")");
        } else {
            std::string header = R"(Basic realm=")";
            header.append(realm);
            header.push_back('"');
            r->addHeader("WWW-Authenticate", header);
        }
    } else {
        std::string header = "Digest ";
        header.append(requestDigestAuthentication(realm));
        r->addHeader("WWW-Authenticate", header);
    }
    send(r);
}

void AsyncWebServerRequest::addInterestingHeader(std::string name)
{
    if (!interestingHeaders_.containsIgnoreCase(name.c_str())) {
        interestingHeaders_.add(std::move(name));
    }
}

void AsyncWebServerRequest::redirect(std::string url)
{
    auto* response = beginResponse(302);
    response->addHeader("Location", std::move(url));
    send(response);
}

/// @brief 发送响应
/// @note 新模型下：respond() 内部通过 OnDrain 决定何时 finish
///       send() 不再检测 response 完成
void AsyncWebServerRequest::send(AsyncWebServerResponse* response)
{
    response_ = response;
    if (response_ == nullptr) {
        conn_->Close();
        onDisconnectfn_();
        return;
    }

    if (response_->sourceValid()) {
        conn_->SetRxTimeout(0);
        response_->respond(this);
    } else {
        delete response_;
        response_ = nullptr;
        send(500);
    }
}

void AsyncWebServerRequest::send(std::string path, std::string contentType,
                                 bool download, AwsTemplateProcessor callback)
{
    if (FILE_EXISTS(path.c_str()) ||
        (!download && FILE_EXISTS(std::string(path + ".gz").c_str()))) {
        send(beginResponse(std::move(path), std::move(contentType),
                           download, std::move(callback)));
    } else {
        send(404);
    }
}

void AsyncWebServerRequest::sendChunked(std::string contentType,
                                        AwsResponseFiller callback,
                                        AwsTemplateProcessor templateCallback)
{
    send(beginChunkedResponse(std::move(contentType), std::move(callback),
                              std::move(templateCallback)));
}

void AsyncWebServerRequest::send_P(int code, std::string contentType,
                                   const uint8_t* content, size_t len,
                                   AwsTemplateProcessor callback)
{
    send(beginResponse_P(code, std::move(contentType), content, len,
                         std::move(callback)));
}

bool AsyncWebServerRequest::hasHeader(const std::string &name) const
{
    if (name.empty()) return false;
    for (const auto &header : headers_) {
        if (0 == strcasecmp(header->name().c_str(), name.c_str())) return true;
    }
    return false;
}

AsyncWebHeader* AsyncWebServerRequest::getHeader(size_t index) const
{
    auto header = headers_.nth(index);
    return header != nullptr ? *header : nullptr;
}

AsyncWebHeader* AsyncWebServerRequest::getHeader(const std::string &name) const
{
    if (name.empty()) return nullptr;
    for (const auto &header : headers_) {
        if (0 == strcasecmp(header->name().c_str(), name.c_str())) return header;
    }
    return nullptr;
}

bool AsyncWebServerRequest::hasParam(const std::string &name, bool post, bool file) const
{
    if (name.empty()) return false;
    for (const auto &param : params_) {
        if (param->name() == name && param->isPost() == post && param->isFile() == file) {
            return true;
        }
    }
    return false;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(size_t index) const
{
    auto param = params_.nth(index);
    return param != nullptr ? *param : nullptr;
}

AsyncWebParameter* AsyncWebServerRequest::getParam(const std::string &name, bool post, bool file) const
{
    if (name.empty()) return nullptr;
    for (const auto &param : params_) {
        if (param->name() == name && param->isPost() == post && param->isFile() == file) {
            return param;
        }
    }
    return nullptr;
}

const std::string &AsyncWebServerRequest::arg(const std::string &name) const
{
    if (name.empty()) return empty_string;
    for (const auto &arg : params_) {
        if (arg->name() == name) return arg->value();
    }
    return empty_string;
}

const std::string &AsyncWebServerRequest::arg(size_t index) const
{
    AsyncWebParameter* param = getParam(index);
    return param != nullptr ? param->value() : empty_string;
}

const std::string &AsyncWebServerRequest::argName(size_t index) const
{
    AsyncWebParameter* param = getParam(index);
    return param != nullptr ? param->name() : empty_string;
}

bool AsyncWebServerRequest::hasArg(const char* name) const
{
    for (const auto &arg : params_) {
        if (arg->name() == name) return true;
    }
    return false;
}

const std::string& AsyncWebServerRequest::header(const char* name) const
{
    auto* header = getHeader(std::string(name));
    return header == nullptr ? empty_string : header->value();
}

const std::string& AsyncWebServerRequest::header(size_t index) const
{
    auto* header = getHeader(index);
    return header == nullptr ? empty_string : header->value();
}

const std::string& AsyncWebServerRequest::headerName(size_t index) const
{
    auto* header = getHeader(index);
    return header == nullptr ? empty_string : header->name();
}

AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(int code, std::string contentType, std::string content)
{
    return new AsyncBasicResponse(code, std::move(contentType), std::move(content));
}

AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(std::string path, std::string contentType,
                                                             bool download, AwsTemplateProcessor callback)
{
    if (FILE_EXISTS(path.c_str()) ||
        (!download && FILE_EXISTS(std::string(path + ".gz").c_str()))) {
        return new AsyncFileResponse(std::move(path), std::move(contentType),
                                     download, std::move(callback));
    }
    return nullptr;
}

AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(std::string contentType, size_t len,
                                                             AwsResponseFiller callback,
                                                             AwsTemplateProcessor templateCallback)
{
    if (callback != nullptr) {
        return new AsyncCallbackResponse(std::move(contentType), len,
                                         std::move(callback), std::move(templateCallback));
    }
    return nullptr;
}

AsyncWebServerResponse* AsyncWebServerRequest::beginChunkedResponse(std::string contentType,
                                                                    AwsResponseFiller callback,
                                                                    AwsTemplateProcessor templateCallback)
{
    if (version_ == 1) {
        return new AsyncChunkedResponse(std::move(contentType), std::move(callback),
                                        std::move(templateCallback));
    }
    return new AsyncCallbackResponse(std::move(contentType), 0, std::move(callback),
                                     std::move(templateCallback));
}

AsyncWebServerResponse* AsyncWebServerRequest::beginResponse_P(int code, std::string contentType,
                                                               const uint8_t *content, size_t len,
                                                               AwsTemplateProcessor callback)
{
    return new AsyncProgmemResponse(code, std::move(contentType), content, len,
                                    std::move(callback));
}

// AsyncWebServerRequest.cc
void AsyncWebServerRequest::send_P(int code, std::string contentType,
                                   const uint8_t* content, size_t len,
                                   const char* contentEncoding,
                                   AwsTemplateProcessor callback)
{
    auto* resp = beginResponse_P(code, std::move(contentType), content, len,
                                 std::move(callback));
    if (resp && contentEncoding) {
        resp->addHeader("Content-Encoding", contentEncoding);
    }
    send(resp);
}

/// @brief 由 response 完成时调用，删除当前 response
/// @note 必须在 OnDrain 触发后调用（此时 SendPool 已空）
void AsyncWebServerRequest::finishResponse()
{
    if (conn_) {
        conn_->OnDrain(nullptr, nullptr);   // ★ 先清 drain 回调，防止悬空
    }
    auto* r = response_;
    response_ = nullptr;
    if (r) {
        delete r;                             // ★ 此处完整类型可见
    }
}

bool AsyncWebServerRequest::append(const void* data, size_t len, uint8_t flags,
     void(*cb)(void*, size_t), void* ctx)
{
    return conn_ && conn_->Append(data, len, flags, cb, ctx);
}

void AsyncWebServerRequest::flush()
{
    if (conn_) conn_->Flush();
}

void AsyncWebServerRequest::close()
{
    if (conn_) conn_->Close();
}




