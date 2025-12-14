#include "AsyncWebServerResponse.h"
#include "../header/AsyncWebHeader.h"
#include "../header/DefaultHeaders.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncClient.h"

const char * WS_STR_CONNECTION = "Connection";
const char * WS_STR_UPGRADE = "Upgrade";
const char * WS_STR_ORIGIN = "Origin";
const char * WS_STR_VERSION = "Sec-WebSocket-Version";
const char * WS_STR_KEY = "Sec-WebSocket-Key";
const char * WS_STR_PROTOCOL = "Sec-WebSocket-Protocol";
const char * WS_STR_ACCEPT = "Sec-WebSocket-Accept";
const char * WS_STR_UUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

AsyncWebServerResponse::AsyncWebServerResponse(uint16_t code, const std::string& contentType)
    : sendContentLength_(true)
    , chunked_(false)
    , code_(code)
    , contentLength_(0)
    , headLength_(0)
    , sentLength_(0)
    , ackedLength_(0)
    , writtenLength_(0)
    , state_(RESPONSE_SETUP)
    , contentType_(contentType)
    , headers_(LinkedList<AsyncWebHeader*>([](AsyncWebHeader* h){ delete h; }))
{
    for (auto header : DefaultHeaders::Instance()) {
        headers_.add(new AsyncWebHeader(header->name(), header->value()));
    }
}

AsyncWebServerResponse::~AsyncWebServerResponse()
{
    headers_.free();
}


/// @brief 组装响应头部（会添加换行\r\n）
std::string AsyncWebServerResponse::assembleHead(uint8_t version)
{
    if (version) {
        addHeader("Accept-Ranges", "none");
        if (chunked_) {
            addHeader("Transfer-Encoding", "chunked");
        }
    }

    std::string out;
    out.reserve(256);       //预先分配部分内存

    out = version == 0 ? "HTTP/1.0" : "HTTP/1.1";
    out += ' ' + std::to_string(code_) + ' ' + responseCodeToString(code_) + "\r\n";

    if (sendContentLength_) {
        out += "Content-Length: " + std::to_string(contentLength_) + "\r\n";
    }
    
    if (!contentType_.empty()) {
        out += "Content-Type: " + contentType_ + "\r\n";
    }

    for(const auto& header : headers_) {
        out += header->name() + ": " + header->value() + "\r\n";
    }
    headers_.free();
    out += "\r\n";
    headLength_ = out.length();

    return out;
}

void AsyncWebServerResponse::addHeader(std::string name, std::string value) {
    headers_.add(new AsyncWebHeader(std::move(name), std::move(value)));
}

void AsyncWebServerResponse::respond(AsyncWebServerRequest* req) {
    state_ = RESPONSE_END;
    req->client_->close();
}


/// @brief 将HTTP状态码转化为字符串
const char* AsyncWebServerResponse::responseCodeToString(uint16_t code)
{
    switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested range not satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version not supported";
    default:  return "Invalid code.";
    }
}