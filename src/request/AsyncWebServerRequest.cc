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
    PARSE_REQ_START,    // 开始解析请求
    PARSE_REQ_HEADERS,  // 解析请求头
    PARSE_REQ_BODY,     // 解析请求体
    PARSE_REQ_END,      // 解析请求结束
    PARSE_REQ_FAIL      // 解析请求失败
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

void AsyncWebServerRequest::init(AsyncWebServer* server, AsyncClient* client)
{
    next_       = nullptr;
    client_     = client;
    server_     = server;
    handler_    = nullptr;
    response_   = nullptr;
    interestingHeaders_.free();  
    onDisconnectfn_     = nullptr;

    tmp_            = empty_string;
    isFragmented_   = false;
    parseState_     = PARSE_REQ_START;
    version_         = 0;
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

    client->set_data_received_handler([](void* r, void* data, size_t len) {
            auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
            req->onData(data, len);
        },
        this
    );
    client->set_error_event_handler([](void* r, int8_t err){
            auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
            req->onErr(err);
        },
        this);
    client->set_ack_event_handler([](void* r, size_t len, uint32_t time) {
            auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
            req->onAck(len, time);
        }, 
        this
    );

    // 没有断开连接业务处理
    // client->set_disconnected_event_handler([](void* r){
    //     },
    //     this
    // );
    // 使用默认发送超时函数（关闭连接）
    // client->set_timeout_event_handler([](void* r, uint32_t time) {
    //         auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
    //         req->onTimeout(time);
    //     },
    //     this
    // );
    
    client->set_poll_event_handler([](void* r) {
            auto* req = reinterpret_cast<AsyncWebServerRequest*>(r);
            req->onPoll();
        },
        this);
    client->set_recycle_handler([](void* arg) {
        auto* req = (AsyncWebServerRequest*)arg;
        req->onDisconnect();
        // 回收
        req->server_->recycleRequest(req);
    }, this);
}

/// @brief 错误回调函数
/// @param error
void AsyncWebServerRequest::onErr(err_t error)
{
    ESP_LOGE(TAG, "Request err, err code=%d", error);
}

/// @brief 数据确认回调
/// @param len
/// @param time
void AsyncWebServerRequest::onAck(size_t len, uint32_t time)
{
    if (response_ != nullptr) {
        if (response_->finished()) {
            auto* response = response_;
            response_ = nullptr;
            delete response;
        } else {
            response_->ack(this, len, time);
        }
    }
}

/// @brief 断开回调函数
void AsyncWebServerRequest::onDisconnect()
{
    if (onDisconnectfn_ != nullptr) {
        onDisconnectfn_();
    }
    server_->internalHandleDisconnect(this);
}

/// @brief 轮询回调函数（确认数据）
void AsyncWebServerRequest::onPoll()
{
    if (response_ != nullptr
            && client_ != nullptr
            && client_->get_send_buffer_size()
            && !(response_->finished())) {
        response_->ack(this, 0, 0);
    }
}

/// @brief 接近零拷贝数据到达处理（仅在分块时拷贝部分），传递字符时遵行[start, end)半开区间
void AsyncWebServerRequest::onData(void* buf, size_t len)
{

    while (true) {
        if (parseState_ < PARSE_REQ_BODY) { // 处理请求行、请求头
            // 获取完整一行数据（\r\n\r\n，后续会去除这些字符故只检查\n即可)
            auto* str = (char*)buf;
            size_t i = 0;
            while (i < len && str[i] != '\n') { i ++; }
            if (i >= len) { 
                // 无换行（跨数据块，先保存数据）
                tmp_.append(str, len);
                isFragmented_ = true;
            } else {
                // 有换行符
                if (isFragmented_) {     // 分片数据被保存
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
        } else if (parseState_ == PARSE_REQ_BODY) { // 处理请求体
            const bool needParse = (handler_ && !(handler_->isRequestHandlerTrivial())); // 定义处理器、且不使用平凡处理器时解析
            if (isMultipart_ ) {            // 是否为文件上传
                if (needParse) {
                    handleMultipartBody(buf, len);
                }
            } else {
                if (parsedLength_ == 0) {   // 数据还没有处理，
                    if (contentType_.starts_with("application/x-www-form-urlencoded")) {            // 标准URL编码表单
                        isPlainPost_ = true;
                    } else if (contentType_ == "text/plain" && __is_param_char(((char *)buf)[0])) {
                        // 兼容解析"text/plain"时，实际内容为表单
                        size_t index = 0;
                        while (index < len && __is_param_char(((char *)buf)[index])) { index++; }
                        if (index < len && ((char *)buf)[index - 1] == '=') {
                            isPlainPost_ = true;
                        }
                    }
                }

                if (isPlainPost_) {   
                    // 普通表单解析
                    if (needParse) {
                        parsePlainPost((uint8_t*)buf, len);
                    } 
                } else {
                    // 非表单数据，使用普通body处理
                    if (handler_) {
                        handler_->handleBody(this, (uint8_t*)buf, len, parsedLength_, contentLength_);
                    }
                } 
            }
            // 记录数据处理的长度
            parsedLength_ += len;

            // 解析结束调用相应处理器处理请求
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


/// @brief 解析接收到的每行数据，[start, end)
/// @param start 本行字符串起始位置
/// @param end 本行字符串结束位置
void AsyncWebServerRequest::parseLine(char* start, char* end) //C++17
{
    // 去除空白字符
    auto* str_end = end;
    while (start < end  && std::isspace(static_cast<unsigned char>(*start))) {
        start ++;
    } 
    if (start < end) { // 在字符串长度不为0时去除结尾空白
        while (end > start && std::isspace(static_cast<unsigned char>(*(--end))));
    }
    end++;  // 指向不包含的字符

    if (parseState_ == PARSE_REQ_START) {
        if (start == str_end) {
            parseState_ = PARSE_REQ_FAIL;
            client_->close();
            ESP_LOGE(TAG, "解析失败: 请求行为空.");
        } else {
            parseReqLine(start, end);
            parseState_ = PARSE_REQ_HEADERS;
        }
        return;
    }

    if (parseState_ == PARSE_REQ_HEADERS) {
        if (start < str_end) {
            parseReqHeader(start, end);
        } else {
            // 遇到空行，请求头处理结束
            server_->internalRewriteRequest(this);      // 执行重写检查（符合时重写）
            server_->internalAttachHandler(this);       // 绑定处理函数
            removeNotInterestingHeaders();      // 过滤不关心的头
            if (expectingContinue_) {
                static const char* response = "HTTP/1.1 100 Continue\r\n\r\n";
                client_->write(response, strlen(response), TCP_WRITE_FLAG_MORE);
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

/// @brief 将指定的数据解析为简单表单
void AsyncWebServerRequest::parsePlainPost(uint8_t* data, size_t len)
{
    size_t pos = 0;
    uint8_t* start;
    uint8_t* end;
    uint8_t* equal_ptr;
    bool isFinal = (parsedLength_ + len == contentLength_);

    while (pos < len) {
        size_t step = 0;
        while (pos + step < len && data[pos + step] != '\0' && (char)(data[pos + step]) != '&') { 
            step ++; 
            
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
    parsedLength_+=len;
}

/// @brief 解析 HTTP 请求行（Request Line），设置相应参数请求方法（如 GET/POST）、URL 路径、查询参数和 HTTP 版本。
/// 接收参数[start, end)
/// 请求行实例："GET /search?query=ESP32&category=development HTTP/1.1"
/*
 * 1. 保存请求的参数至参数链：query=ESP32&category=development
 * 2. 保存访问的URL至url_：/search
 * 3. 设置HTTP版本：1=HTTP/1.0
 * 4. 保存请求方法：method_
 * 
*/
void AsyncWebServerRequest::parseReqLine(char* start, char* end)
{
    auto* space1 = start;
    while (space1 < end && *space1 != ' ') space1++;
    auto* space2 = space1 + 1;
    while (space2 < end && *space2 != ' ') space2++;
    
    // 解析HTTP方法
    /*
    * GET      : G|E|3
    * POST     : P|O|4
    * PUT      : P|U|3
    * PATCH    : P|A|5
    * HEAD     : H|E|4
    * DELETE   : D|E|6
    * OPTIONS  : O|P|7
    */
    uint8_t method_len = space1 - start;
    auto hash = (start[0]<<16) | (method_len < 2 ? 0 : (start[1]<<8)) | method_len;
    switch (hash) {
        case ('G'<<16) | ('E'<<8) | 3 :  method_ = HTTP_GET;     break; // "GET"
        case ('P'<<16) | ('O'<<8) | 4 :  method_ = HTTP_POST;    break; // "POST"
        case ('D'<<16) | ('E'<<8) | 6 :  method_ = HTTP_DELETE;  break; // "DELETE"
        case ('P'<<16) | ('U'<<8) | 3 :  method_ = HTTP_PUT;     break; // "PUT"
        case ('P'<<16) | ('A'<<8) | 5 :  method_ = HTTP_PATCH;   break; // "PATCH"
        case ('H'<<16) | ('E'<<8) | 4 :  method_ = HTTP_HEAD;    break; // "HEAD"
        case ('O'<<16) | ('P'<<8) | 7 :  method_ = HTTP_OPTIONS; break; // "OPTIONS"
        default: method_ = HTTP_ANY; // 或抛出错误
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

    if (memcmp((void*)(space2 + 1), "HTTP/1.0", 8) == 0) {
        version_ = 1;
    }
}

/// @brief 获取请求行中的GET参数，存储到参数列表中，[start, end)
/// query=ESP32&category=development
/// name=John&age=&active
void AsyncWebServerRequest::addGetParams(const char* start, const char* end)
{
    while (start < end) {
        auto* equal_ptr = start;
        while (equal_ptr < end && *equal_ptr != '=') equal_ptr++;
        auto* ampersan_ptr = equal_ptr + 1;
        while (ampersan_ptr < end && *ampersan_ptr != '&') ampersan_ptr++;  // 查找'&'符号

        if (equal_ptr >= end - 1) { // 没有等号或等号结尾
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

/// @brief 解析URL中编码的字符串[start, end)
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
        } else {
            decoded = *start;
        }
        decoded_str.push_back(decoded);
    }

    return decoded_str;
}


enum HeaderID : uint32_t {
    // hash = char[0]<<24 | char[1]<<16 | char[2]<<8 | len

    // Accept family
    kAccept         = ('a'<<24) | ('c'<<16) | ('c'<<8) | 6,
    kAcceptCharset  = ('a'<<24) | ('c'<<16) | ('c'<<8) | 14,  // "accept-charset"
    kAcceptEncoding = ('a'<<24) | ('c'<<16) | ('c'<<8) | 15,  // "accept-encoding"
    kAcceptLanguage = ('a'<<24) | ('c'<<16) | ('c'<<8) | 16,  // "accept-language"

    // Standard headers
    kAuthorization  = ('a'<<24) | ('u'<<16) | ('t'<<8) | 13,  // "authorization"
    kCacheControl   = ('c'<<24) | ('a'<<16) | ('c'<<8) | 13,  // "cache-control"
    kConnection     = ('c'<<24) | ('o'<<16) | ('n'<<8) | 10,  // "connection"
    kCookie         = ('c'<<24) | ('o'<<16) | ('o'<<8) | 6,   // "cookie"
    kDate           = ('d'<<24) | ('a'<<16) | ('t'<<8) | 4,   // "date"
    kExpect         = ('e'<<24) | ('x'<<16) | ('p'<<8) | 6,   // "expect"
    kForwarded      = ('f'<<24) | ('o'<<16) | ('r'<<8) | 9,   // "forwarded"
    kFrom           = ('f'<<24) | ('r'<<16) | ('o'<<8) | 4,   // "from"
    kHost           = ('h'<<24) | ('o'<<16) | ('s'<<8) | 4,   // "host"
    // kIfMatch_Range = "if-range"/"if-match"⚠️
    kIfMatch_Range  = ('i'<<24) | ('f'<<16) | ('-'<<8) | 8,     
    kIfModifySince  = ('i'<<24) | ('f'<<16) | ('-'<<8) | 17,  // "if-modified-since"
    kIfNoneMatch    = ('i'<<24) | ('f'<<16) | ('-'<<8) | 13,  // "if-none-match"
    kIfUnmodifySince= ('i'<<24) | ('f'<<16) | ('-'<<8) | 19,  // "if-unmodified-since"
    kMaxForwards    = ('m'<<24) | ('a'<<16) | ('x'<<8) | 12,  // "max-forwards"
    kOrigin         = ('o'<<24) | ('r'<<16) | ('i'<<8) | 6,   // "origin"
    kPragma         = ('p'<<24) | ('r'<<16) | ('a'<<8) | 6,   // "pragma"
    kProxyAuth      = ('p'<<24) | ('r'<<16) | ('o'<<8) | 20,  // "proxy-authorization"
    kReferer        = ('r'<<24) | ('e'<<16) | ('f'<<8) | 7,   // "referer"
    kTE             = ('t'<<24) | ('e'<<16) | (0  <<8) | 2,   // "te" (len=2, c2=0)
    kUserAgent      = ('u'<<24) | ('s'<<16) | ('e'<<8) | 10,  // "user-agent"
    kUpgrade        = ('u'<<24) | ('p'<<16) | ('g'<<8) | 7,   // "upgrade"
    kVia            = ('v'<<24) | ('i'<<16) | ('a'<<8) | 3,   // "via"
    kWarning        = ('w'<<24) | ('a'<<16) | ('r'<<8) | 7,   // "warning"

    // Content headers
    kContentMD5         = ('c'<<24) | ('o'<<16) | ('n'<<8) | 11,    // "content-md5"
    kContentType        = ('c'<<24) | ('o'<<16) | ('n'<<8) | 12,    // "content-type"
    kContentRange       = ('c'<<24) | ('o'<<16) | ('n'<<8) | 13,    // "content-range"
    kContentLength      = ('c'<<24) | ('o'<<16) | ('n'<<8) | 14,    // "content-length"
    // kCtnEncode_Locate_Language = "content-encoding"/"content-location"/"content-language"⚠️
    kCtnEncode_Locate_Language  = ('c'<<24) | ('o'<<16) | ('n'<<8) | 16,

    // Security & privacy
    kDNT                = ('d'<<24) | ('n'<<16) | ('t'<<8) | 3,   // "dnt"
    kUpgradeInsecureReq = ('u'<<24) | ('p'<<16) | ('g'<<8) | 25, // "upgrade-insecure-requests"

    // CORS / Preflight
    kACLReqMethod   = ('a'<<24) | ('c'<<16) | ('c'<<8) | 24, // "access-control-request-method"
    kACLReqHeaders  = ('a'<<24) | ('c'<<16) | ('c'<<8) | 27, // "access-control-request-headers"

    // Attention!!!!Sec-Fetch-* (all len=14, prefix "sec") ⚠️
    // KSecFectch = sec-fetch-mode or sec-fetch-site or sec-fetch-user or sec-fetch-dest
    kSecFetch       = ('s'<<24) | ('e'<<16) | ('c'<<8) | 14,    // Attention!!!!

    // X-Forwarded-* and custom
    kXForwardedFor  = ('x'<<24) | ('-'<<16) | ('f'<<8) | 16, // "x-forwarded-for"
    kXForwardedHost = ('x'<<24) | ('-'<<16) | ('f'<<8) | 17, // "x-forwarded-host"
    kXForwardedProto= ('x'<<24) | ('-'<<16) | ('f'<<8) | 18, // "x-forwarded-proto"
    kXRealIP        = ('x'<<24) | ('-'<<16) | ('r'<<8) | 10, // "x-real-ip"
    kXRequestedWith = ('x'<<24) | ('-'<<16) | ('r'<<8) | 18, // "x-requested-with"
    kXCSRFToken     = ('x'<<24) | ('-'<<16) | ('c'<<8) | 12, // "x-csrf-token"
    kXXSRFToken     = ('x'<<24) | ('-'<<16) | ('x'<<8) | 12, // "x-xsrf-token"
    kXAPIKey        = ('x'<<24) | ('-'<<16) | ('a'<<8) | 9,  // "x-api-key"

    kUnknown        = 0 // 哨兵值，表示未知
};



/// @brief 解析解析每一行请求头中的字符串，以便后续处理（如路由、认证、解析请求体等）
/// 将 Host, Content-Type, Authorization 等其存储到类的成员变量headers_中.[start, end)
/*
 * Host: example.com
 * Content-Type: multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW
 * Content-Length: 723
 * Expect: 100-continue
 * Authorization: Basic eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.XXXXXXXXXXXXXXXXXX
 * 
*/
bool AsyncWebServerRequest::parseReqHeader(const char* start, const char* end)
{
    const auto* colon = start;
    while (colon < end && *colon != ':') colon++;
    auto value_start = colon + 1;
    while ((value_start < end) && (std::isspace(*value_start))) value_start++;

    // must have value
    if (end - value_start > 1) {
        size_t name_len = colon - start;
        if (name_len > 63)  return false;   // 正常请求头不超过该值的
        uint32_t hash_code = name_len;
        if (name_len > 2) {
            hash_code |= tolower(start[2]) << 8;
            hash_code |= tolower(start[1]) << 16;
            hash_code |= tolower(start[0]) << 24;
        } else if (name_len > 1) {
            hash_code |= tolower(start[1]) << 16;
            hash_code |= tolower(start[0]) << 24;
        } else {
            hash_code |= tolower(start[0]) << 24;
        }

        std::string name(start, name_len);
        std::string value(value_start, end-value_start);
        switch (hash_code)
        {
          case kHost:
            host_ = value;
            break;
          case kContentType:
            contentType_ = value.substr(0, value.find(';'));
            if (value.starts_with("multipart")) {
                boundary_ = value.substr(value.find('=') + 1);
                boundary_.erase(std::remove(boundary_.begin(), boundary_.end(), '"'), boundary_.end());
                boundary_ += "--";          // 添加结束标志
                isMultipart_ = true;
            }
            break;
          case kContentLength:
            contentLength_ = atoi(value.c_str());
            break;
          case kExpect:
            if (value == "100-continue") {
                expectingContinue_ = true;
            }
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
            if (0 == strcasecmp(value.c_str(), "websocket")) {
                reqconntype_ = RCT_WS;
            }
            break;
          case kAccept:
            if (strContains(value, "text/event-stream", false)) {
                reqconntype_ = RCT_EVENT;
            }
            break;
        }
        headers_.add(new AsyncWebHeader(std::move(name), std::move(value)));
    }

    return true;
}

/// @brief 过滤不关心的请求头(根据interestingHeaders_配置，去除headers_的头)
void AsyncWebServerRequest::removeNotInterestingHeaders()
{
    if (interestingHeaders_.containsIgnoreCase("ANY")) {
        return;
    }
    for (const auto &header : headers_) {
        if (!interestingHeaders_.containsIgnoreCase(header->name().c_str())) {
            headers_.remove(header);
        }
    }
}


enum MultipartState {
    MP_START,                // 初始状态：期待第一个 '--'
    MP_MATCHING_BOUNDARY,    // 正在匹配 boundary
    MP_READING_HEADERS,      // 读取 part 的 headers
    MP_READING_BODY,         // 读取 body 数据(boundary结束)
    MP_FINISHED,
    MP_ERROR
};
/// @brief 解析多部分Body
/// @param buf （原始数据，注意可能会分片）
/// @param len 
/*
 *  ------WebKitFormBoundary7MA4YWxkTrZu0gW--
 *  Content-Disposition: form-data; name="username"
 * 
 *  JohnDoe
 *  ------WebKitFormBoundary7MA4YWxkTrZu0gW--
 *  Content-Disposition: form-data; name="uploadFile"; filename="example.png"
 *  Content-Type: image/png
*/
void AsyncWebServerRequest::handleMultipartBody(void* buf, size_t len)
{
    uint8_t* data = static_cast<uint8_t*>(buf);

    // 初始化各个参数
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
            // 缓存未完全的行数据
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
            if (nl > pos && data[nl - 1] == '\r') {
                line_end--;
            }
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

    // 这里有可能会转回MP_READING_HEADERS，pos为处理的起始位置
    if (multiParseState_ == MP_READING_BODY) {
        auto boundary_len = boundary_.length();
        while (pos < len && multiParseState_ == MP_READING_BODY) {
            size_t nl = pos;
            while (nl < len && data[nl] != boundary_[boundaryMatchLen_]) { nl ++; }    

            if (nl >= len) {
                // 真实数据，[pos, len)
                handleUpload(data + pos, len, false);
                return;
            } else {
                // 开始部分匹配
                auto match_len = std::min(len - nl, boundary_len);      // 本次匹配长度
                int i = 0;
                for (; i < match_len; i++) {
                    if (data[nl + i] != boundary_[boundaryMatchLen_++]) {
                        break;
                    }
                }
                if (boundaryMatchLen_ >= boundary_len) { // 匹配到boundary，也可能是结束符
                    multiParseState_ = boundaryMatchLen_ == boundary_len ? MP_FINISHED : MP_READING_HEADERS;
                    // 真实数据[pos, pos+nl-1)
                    if (multiParseState_ == MP_READING_HEADERS) {
                        pos += nl; 
                        handleUpload(data + pos, pos+nl-1, true);       
                        goto handle_header_again;
                    } else {
                        handleUpload(data + pos, pos+nl-1, false);
                        return;
                    }
                } 
                // 匹配不成功
                if (pos +i == len) {    // 数据用尽，等等继续匹配
                    // / 真实数据，[pos, len)
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

/// @brief 处理多部分的一行数据（不含multipart body部分）[start,end)。。。。。。。。。。。可以优化，发生了数据拷贝
void AsyncWebServerRequest::parseMultiPartLine(uint8_t* start, uint8_t* end)
{
    switch (multiParseState_) {
      case MP_START :
        multiParseState_ = memcmp((void*)start, boundary_.c_str(), end - start) == 0 ? MP_READING_HEADERS : MP_ERROR;
        break;
      case MP_READING_HEADERS :
        size_t line_len = end - start;
        if (tmp_.empty()) {
            // 数据不是在缓存中，加载到缓存
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

// 处理表单中的数据
void AsyncWebServerRequest::handleUpload(uint8_t* data, size_t len, bool last)
{
    if (itemIsFile_) {
        if (handler_) {
            handler_->handleUpload(this, 
                itemFileName_, 
                itemSize_,   // 当前索引
                data,
                len,
                last
            );
            itemSize_ += len;
        }
    } else {
        itemValue_.append((const char*)data, len);
    }
}


const char* AsyncWebServerRequest::methodToString() const
{
    switch (method_) {
      case HTTP_GET:    return "GET";
      case HTTP_POST:   return "POST";
      case HTTP_DELETE: return "DELETE";
      case HTTP_PUT:    return "PUT";
      case HTTP_PATCH:  return "PATCH";
      case HTTP_HEAD:   return "HEAD";
      case HTTP_OPTIONS:return "OPTIONS";
      default:          return (method_ == HTTP_ANY) ? "ANY" : "UNKNOWN";
    }
}


const char* AsyncWebServerRequest::requestedConnTypeToString() const
{
    switch (reqconntype_) {
      case RCT_NOT_USED:    return "RCT_NOT_USED";
      case RCT_DEFAULT:     return "RCT_DEFAULT";
      case RCT_HTTP:        return "RCT_HTTP";
      case RCT_WS:          return "RCT_WS";
      case RCT_EVENT:       return "RCT_EVENT";
      default:              return "ERROR";
    }
}

/// @brief 检查当前请求的连接类型是否符合传入的预期类型
bool AsyncWebServerRequest::isExpectedRequestedConnType(RequestedConnectionType erct1, RequestedConnectionType erct2, RequestedConnectionType erct3)
{
    bool res = false;
    if ((erct1 != RCT_NOT_USED) && (erct1 == reqconntype_)) {
        res = true;
    }
    if ((erct2 != RCT_NOT_USED) && (erct2 == reqconntype_)) {
        res = true;
    }
    if ((erct3 != RCT_NOT_USED) && (erct3 == reqconntype_)) {
        res = true;
    }
    return res;
}

/// @brief 进行用户信息认证
bool AsyncWebServerRequest::authenticate(const char * hash)
{
    if (hash == nullptr || authorization_.empty()) {
        return false;
    }

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
        if (pos2 == str_len) {
            return false;
        }
        std::string name;
        std::string realm;
        const char* hash_str = nullptr;

        name.assign(hash, pos1);
        realm.assign(hash + pos1 + 1, pos2 - pos1 - 1);
        hash_str = hash + pos2 + 1;

        return checkDigestAuthentication(authorization_.c_str(),
            methodToString(),
            name.c_str(),
            hash_str,
            realm.c_str(),
            true,
            nullptr,
            nullptr,
            nullptr    
        );
    }

    return authorization_ == hash;
}

/// @brief 验证HTTP请求的身份信息
bool AsyncWebServerRequest::authenticate(const char* username, const char* password, const char* realm, bool passwordIsHash)
{
    if (authorization_.length()) {
        if (isDigest_) {
            return checkDigestAuthentication(authorization_.c_str(),
                                             methodToString(),
                                             username,
                                             password,
                                             realm,
                                             passwordIsHash,
                                             nullptr,
                                             nullptr,
                                             nullptr
                                            );
        } else if (!passwordIsHash) {
            return checkBasicAuthentication(
                       authorization_.c_str(),
                       username,
                       password
                   );
        } else {
            return authorization_ == password;
        }
    }

    return false;
}

/// @brief 向客户端发送HTTP 401 Unauthorized响应，附加WWW-Authenticate头，要求进行身份认证
/// @param realm 认证域（标识受保护资源的范围）
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

/// @brief 向关注的头请求头列表中添加头部
void AsyncWebServerRequest::addInterestingHeader(std::string name)
{
    if (!interestingHeaders_.containsIgnoreCase(name.c_str())) {
        interestingHeaders_.add(std::move(name));
    }
}

/// @brief 向客户端发送资源被重定向的响应
/// @param url 资源重定向后的路径
void AsyncWebServerRequest::redirect(std::string url)
{
    auto* response = beginResponse(302);
    response->addHeader("Location", std::move(url));
    send(response);
}

/// @brief 验证响应的合法性后，发送相应的响应内容（可能是派生类的respond()函数）
/// @param response 响应基类指针（传递参数可能是派生类）
void AsyncWebServerRequest::send(AsyncWebServerResponse* response)
{
    response_ = response;
    if (response_ == nullptr) {
        client_->close();
        onDisconnectfn_();
        return;
    }
    
    if (response_->sourceValid()) {
        client_->set_rx_timeout_second(0);
        response_->respond(this);
    } else {
        delete response_;
        response_ = nullptr;
        send(500);
    }
}

/// @brief 发送一个文件响应
/// @param path 文件路径
/// @param contentType 资源类型
/// @param download 是否为下载模式
/// @param callback 采用的模板处理函数
void AsyncWebServerRequest::send(std::string path, std::string contentType, bool download, AwsTemplateProcessor callback)
{
    if (FILE_EXISTS(path.c_str()) || (!download && FILE_EXISTS(std::string(path + ".gz").c_str()))) {
        send(beginResponse(std::move(path), std::move(contentType), download, std::move(callback)));
    } else {
        send(404);
    }
}

/// @brief 发送一个分片响应
void AsyncWebServerRequest::sendChunked(std::string contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback)
{
    send(beginChunkedResponse(std::move(contentType), std::move(callback), std::move(templateCallback)));
}

/// @brief 发送一个内存数据响应
void AsyncWebServerRequest::send_P(int code, std::string contentType, const uint8_t* content, size_t len, AwsTemplateProcessor callback)
{
    send(beginResponse_P(code, std::move(contentType), content, len, std::move(callback)));
}



/// @brief 检查是否包含某个请求头
bool AsyncWebServerRequest::hasHeader(const std::string &name) const
{
    if (name.empty()) {
        return false;
    }
    for (const auto &header : headers_) {
        if (0 == strcasecmp(header->name().c_str(), name.c_str())) {
            return true;
        }
    }
    return false;
}

/// @brief 获取请求中指定索引的请求头
AsyncWebHeader* AsyncWebServerRequest::getHeader(size_t index) const
{
    auto header = headers_.nth(index);
    return header != nullptr ? *header : nullptr;
}

/// @brief 根据指定的名称获取指定的请求头
AsyncWebHeader* AsyncWebServerRequest::getHeader(const std::string &name) const
{
    if (name.empty()) {
        return nullptr;
    }

    for (const auto &header : headers_) {
        if (0 == strcasecmp(header->name().c_str(), name.c_str())) {
            return header;
        }
    }
    return nullptr;
}

/// @brief 检查HTTP请求中是否存在指定的参数
/// @param name 参数名
/// @param post 为TRUE时，name是否为POST请求体中参数
/// @param file 为TRUE时，name是否为文件上传中参数
bool AsyncWebServerRequest::hasParam(const std::string &name, bool post, bool file) const
{
    if (name.empty()) {
        return false;
    }
    for (const auto &param : params_) {
        if (param->name() == name && param->isPost() == post && param->isFile() == file) {
            return true;
        }
    }
    return false;
}

/// @brief 获取请求参数列表中指定索引的参数
AsyncWebParameter* AsyncWebServerRequest::getParam(size_t index) const
{
    auto param = params_.nth(index);
    return param != nullptr ? *param : nullptr;
}

/// @brief 获取HTTP请求中特定的参数
AsyncWebParameter* AsyncWebServerRequest::getParam(const std::string &name, bool post, bool file) const
{
    if (name.empty()) {
        return nullptr;
    }
    for (const auto &param : params_) {
        if (param->name() == name && param->isPost() == post && param->isFile() == file) {
            return param;
        }
    }
    return nullptr;
}

/// @brief 从参数列表中获取指定名称的参数的值
const std::string &AsyncWebServerRequest::arg(const std::string &name) const
{
    if (name.empty()) {
        return empty_string;
    }
    for (const auto &arg : params_) {
        if (arg->name() == name) {
            return arg->value();
        }
    }
    return empty_string;
}

/// @brief 获取指定索引参数的参数值
const std::string &AsyncWebServerRequest::arg(size_t index) const
{
    AsyncWebParameter* param = getParam(index);
    return param != nullptr ? param->value() : empty_string;
}

/// @brief 获取指定索引参数的参数名
const std::string &AsyncWebServerRequest::argName(size_t index) const
{
    AsyncWebParameter* param = getParam(index);
    return param != nullptr ? param->name() : empty_string;
}

/// @brief 检查是否含有指定名字的参数
bool AsyncWebServerRequest::hasArg(const char* name) const
{
    for (const auto &arg : params_) {
        if (arg->name() == name) {
            return true;
        }
    }

    return false;
}

/// @brief 获取请求中指定名字的请求头对应的值
const std::string& AsyncWebServerRequest::header(const char* name) const
{
    auto* header = getHeader(std::string(name));
    return header == nullptr ? empty_string : header->value();
}

/// @brief 获取请求中指定索引的请求头对应的值
const std::string& AsyncWebServerRequest::header(size_t index) const
{
    auto* header = getHeader(index);
    return header == nullptr ? empty_string : header->value();
}

/// @brief 获取指定索引的请求头对应的名称
const std::string& AsyncWebServerRequest::headerName(size_t index) const
{
    auto* header = getHeader(index);
    return header == nullptr ? empty_string : header->name();
}

/// @brief 构建一个基本响应
/// @param code 响应状态码
/// @param contentType 内容类型
/// @param content 响应内容
AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(int code, std::string contentType, std::string content)
{
    return new AsyncBasicResponse(code, std::move(contentType), std::move(content));
}

/// @brief 构建一个文件响应
/// @param path 文件路径
/// @param contentType 内容类型
/// @param download 是否为下载模式
/// @param callback 模板处理函数
AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(std::string path, std::string contentType, bool download, AwsTemplateProcessor callback)
{
    if (FILE_EXISTS(path.c_str()) || (!download && FILE_EXISTS(std::string(path + ".gz").c_str()))) {
        return new AsyncFileResponse(std::move(path), std::move(contentType), download, std::move(callback));
    }
    return nullptr;
}

/// @brief 构造一个回调响应
/// @param contentType 内容类型
/// @param len 响应体长度
/// @param callback 用于执行的回调函数
/// @param templateCallback 模板处理函数
AsyncWebServerResponse* AsyncWebServerRequest::beginResponse(std::string contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback)
{
    if (callback != nullptr) {
        return new AsyncCallbackResponse(std::move(contentType), len, std::move(callback), std::move(templateCallback));
    }
    return nullptr;
}

/// @brief 构建分片响应
/// @param contentType 内容类型
/// @param callback 用于填充分块的回调函数
/// @param templateCallback 模板处理函数
AsyncWebServerResponse* AsyncWebServerRequest::beginChunkedResponse(std::string contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback)
{
    if (version_) {
        return new AsyncChunkedResponse(std::move(contentType), std::move(callback), std::move(templateCallback));
    }
    return new AsyncCallbackResponse(std::move(contentType), 0, std::move(callback), std::move(templateCallback));
}

/// @brief 构建一个内部存储响应
/// @param contentType 内容类型
/// @param content 内容指针
/// @param callback 用于填充分块的回调函数
/// @param templateCallback 模板处理函数
AsyncWebServerResponse* AsyncWebServerRequest::beginResponse_P(int code, std::string contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback){
  return new AsyncProgmemResponse(code, std::move(contentType), content, len, std::move(callback));
}

























