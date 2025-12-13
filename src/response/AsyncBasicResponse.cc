#include "AsyncBasicResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncClient.h"
#include <string>


AsyncBasicResponse::AsyncBasicResponse(uint16_t code, const std::string& contentType, const std::string& content)
    : AsyncWebServerResponse(code, contentType)
    , headerSent_(0)
    , countentSent_(0)
    , content_(content)
{
    contentLength_ = content_.length();
    if (contentLength_) {
        if(!contentType_.length()) {
            contentType_ = "text/plain";
        }
    }
    addHeader("Connection", "close");
}

/// @brief 将基本响应发送出去
/// @param req
inline void AsyncBasicResponse::respond(AsyncWebServerRequest* req)
{
    if (state_ != RESPONSE_SETUP) {
        return;
    }
    state_ = RESPONSE_HEADERS;
    auto header_ = assembleHead(req->version_);
    
    // 立即尝试发送
    ack(req, 0, 0);
}


size_t AsyncBasicResponse::ack(AsyncWebServerRequest *req, size_t len, uint32_t time)
{
    ackedLength_ += len;
    auto* client_ = req->client_;
    if (!sourceValid()) {
        state_ = RESPONSE_FAILED;
        client_->close();
        return 0;
    }

    size_t totalSent = 0;
    size_t space = client_->get_send_buffer_size();


    if (state_ == RESPONSE_HEADERS) {
        auto headerRemaining = header_.length() - headerSent_;
        if (headerRemaining > 0) {
            auto toSend = std::min(headerRemaining, space);
            const char* data = header_.c_str() + headerSent_;
            size_t sent = client_->add(data, toSend);
            if (sent) {
                headerSent_ += sent;
                totalSent += sent;
                writtenLength_ += sent;
            }
        }

        if (headerSent_ >= header_.length()) {
            state_ = RESPONSE_CONTENT;
            space -= totalSent;
        } else {
            client_->send();
            return totalSent;
        }
        
    }

    if (state_ == RESPONSE_CONTENT) {
        auto contentRemaining = content_.length() - countentSent_;
        if (contentRemaining > 0 && space) {
            auto toSend = std::min(contentRemaining, space);
            const char* data = content_.c_str() + countentSent_;
            auto sent = client_->add(data, toSend);
            if (sent) {
                countentSent_ += sent;
                sentLength_ += sent;
                writtenLength_ += sent;
            }
        }

        if (countentSent_ >= content_.length()) {
            state_ = RESPONSE_WAIT_ACK;
        }
    }

    if (state_ == RESPONSE_WAIT_ACK) {
        if (ackedLength_ >= writtenLength_) {
            state_ = RESPONSE_END;
        }
    }

    client_->send();
    return totalSent;
} 
