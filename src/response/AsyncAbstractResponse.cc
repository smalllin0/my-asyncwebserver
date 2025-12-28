#include "AsyncAbstractResponse.h"
#include "../request/AsyncWebServerRequest.h"
#include "AsyncClient.h"

AsyncAbstractResponse::AsyncAbstractResponse(AwsTemplateProcessor cb)
    : callback_(cb)
    , headerSent_(0)
{
    if (cb) {
        contentLength_ = 0;
        sendContentLength_ = 0;
        chunked_ = true;
    }
}

/// @brief 发送响应
void AsyncAbstractResponse::respond(AsyncWebServerRequest* req)
{
    addHeader("Connection", "close");
    header_ = assembleHead(req->version_);
    state_ = RESPONSE_HEADERS;
    ack(req, 0, 0);
}


size_t AsyncAbstractResponse::ack(AsyncWebServerRequest* req, size_t len,  uint32_t time)
{
    auto* client = req->client_;
    if (!sourceValid()) {
        client->close();
        return 0;
    }

    ackedLength_ += len;
    auto header_length = header_.length();
    if (ackedLength_ >= header_length && !header_.empty()) {
        std::string().swap(header_);    // 释放头部空间
    }

    
    size_t sent_bytes = 0;
    auto space = client->get_send_buffer_size();
    if (state_ == RESPONSE_HEADERS) {
        auto headerRemaining = header_length - headerSent_;
        if (headerRemaining > 0) {
            auto toSend = std::min(headerRemaining, space);
            size_t sent = client->add(header_.c_str() + headerSent_, toSend, TCP_WRITE_FLAG_COPY);
            if (sent) {
                headerSent_ += sent;
                sent_bytes += sent;
                writtenLength_ += sent;
                space -= sent;
            }
        }

        if (headerSent_ >= header_length) {
            // 头部数据已完全写入
            state_ = RESPONSE_CONTENT;   
            if ((space == 0) || (chunked_ && (space < 8))) {    // 空间用尽、不足够chunked
                client->send();
                return sent_bytes;
            }
        } else {     
            // 头部数据未完全写入                               
            client->send();
            return sent_bytes;
        }
        
    }

    // 发送响应体
    if (state_ == RESPONSE_CONTENT && (space > 0)) {
        if(chunked_ && space < 8) {
            return 0;
        }
        // 获取本次发送需要的缓冲区大小
        size_t buffer_size = 0;
        size_t data_size = 0;
        if (contentLength_) {
            buffer_size = std::min(contentLength_ - sentLength_, space);
            data_size = buffer_size;
            if (chunked_) buffer_size += 8;
        } else {
            //..................存在问题！！！
        }
        if (buffer_size > buffer_.size()) {
            buffer_.resize(buffer_size);
        }

        // 填充数据
        auto* buf = buffer_.data();
        auto read_len = fillBufferAndProcessTemplates(chunked_ ? buf + 6 : buf, data_size);
        if (read_len == RESPONSE_TRY_AGAIN) {
            return 0;
        } 
        if (chunked_) {
            // 填充头尾
            sprintf((char*)buf, "%04x\r", read_len);   // FFFF\r\0
            buf[5] = '\n';
            buf[read_len + 6] = '\r'; 
            buf[read_len + 7] = '\n'; 
        }

        // 向客户端写入数据
        auto write_len = client->write((const char*)buf, buffer_size);
        writtenLength_ += write_len;
        sentLength_ += read_len;

        if ((chunked_ && read_len == 0)                         // chunked,时本次无数据
            || (!sendContentLength_ && read_len == 0)          // 无声明长度（Sever-Sent Events、动态流），本次无数据
            || (!chunked_ && sentLength_ == contentLength_)) {  // 非chunked,发送的长度超过声明长度
                state_ = RESPONSE_WAIT_ACK;
        }
        
        return write_len;
    } else if (state_ == RESPONSE_WAIT_ACK) {
        if (!sendContentLength_ || ackedLength_ >= writtenLength_) {
            state_ = RESPONSE_END;
            if (!chunked_ && !sendContentLength_) {
                client->close();
            }
        }
    }

    return 0;
}


/// @brief 从缓存（文件）中读取指定字节的数据到data中
size_t AsyncAbstractResponse::readDataFromCacheOrContent(uint8_t* data, const size_t len)
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

/// @brief 加载及模板处理函数..............需要优化
size_t AsyncAbstractResponse::fillBufferAndProcessTemplates(uint8_t* data, size_t len)
{
// 没有回调函数>>>不处理模板
    if (!callback_) {
        return fillBuffer(data, len);
    }

    const size_t originalLen = len;
    len = readDataFromCacheOrContent(data, len);

    uint8_t* pTemplateStart = data;
    while ((pTemplateStart < &data[len]) && (pTemplateStart = (uint8_t *)memchr(pTemplateStart, CONFIG_TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart + 1))) { // data[0] ... data[len - 1]
        uint8_t* pTemplateEnd = (pTemplateStart < &data[len - 1]) ? (uint8_t*)memchr(pTemplateStart + 1, CONFIG_TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart) : nullptr;
        uint8_t buf[CONFIG_TEMPLATE_PARAM_NAME_LENGTH + 1];
        std::string paramName;
        if (pTemplateEnd) {
            const size_t paramNameLength = sizeof(buf) - 1 < pTemplateEnd - pTemplateStart - 1 ? sizeof(buf) - 1 : pTemplateEnd - pTemplateStart - 1;
            if (paramNameLength) {
                memcpy(buf, pTemplateStart + 1, paramNameLength);
                buf[paramNameLength] = 0;
                paramName = std::string(reinterpret_cast<char*>(buf));
            } else { // double percent sign encountered, this is single percent sign escaped.
                memmove(pTemplateEnd, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
                len += readDataFromCacheOrContent(&data[len - 1], 1) - 1;
                ++pTemplateStart;
            }
        } else if (&data[len - 1] - pTemplateStart + 1 < CONFIG_TEMPLATE_PARAM_NAME_LENGTH + 2) { // closing placeholder not found, check if it's in the remaining file data
            memcpy(buf, pTemplateStart + 1, &data[len - 1] - pTemplateStart);
            const size_t readFromCacheOrContent = readDataFromCacheOrContent(buf + (&data[len - 1] - pTemplateStart), CONFIG_TEMPLATE_PARAM_NAME_LENGTH + 2 - (&data[len - 1] - pTemplateStart + 1));
            if (readFromCacheOrContent) {
                pTemplateEnd = (uint8_t*)memchr(buf + (&data[len - 1] - pTemplateStart), CONFIG_TEMPLATE_PLACEHOLDER, readFromCacheOrContent);
                if (pTemplateEnd) {
                    *pTemplateEnd = 0;
                    paramName = std::string(reinterpret_cast<char*>(buf));
                    cache_.insert(cache_.begin(), pTemplateEnd + 1, buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
                    pTemplateEnd = &data[len - 1];
                } else { // closing placeholder not found in file data, store found percent symbol as is and advance to the next position
                    cache_.insert(cache_.begin(), buf + (&data[len - 1] - pTemplateStart), buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
                    ++pTemplateStart;
                }
            } else {
                ++pTemplateStart;
            }
        } else {
            ++pTemplateStart;
        }

        if (paramName.length()) {
            const std::string paramValue(callback_(paramName));
            const char* pvstr = paramValue.c_str();
            const unsigned int pvlen = paramValue.length();
            const size_t numBytesCopied = std::min(pvlen, static_cast<unsigned int>(&data[originalLen - 1] - pTemplateStart + 1));
            // make room for param value
            // 1. move extra data to cache if parameter value is longer than placeholder AND if there is no room to store
            if ((pTemplateEnd + 1 < pTemplateStart + numBytesCopied) && (originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1) < len)) {
                cache_.insert(cache_.begin(), &data[originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1)], &data[len]);
                //2. parameter value is longer than placeholder text, push the data after placeholder which not saved into cache further to the end
                memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[originalLen] - pTemplateStart - numBytesCopied);
                len = originalLen; // fix issue with truncated data, not sure if it has any side effects
            } else if (pTemplateEnd + 1 != pTemplateStart + numBytesCopied)
                //2. Either parameter value is shorter than placeholder text OR there is enough free space in buffer to fit.
                //   Move the entire data after the placeholder
            {
                memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
            }
            // 3. replace placeholder with actual value
            memcpy(pTemplateStart, pvstr, numBytesCopied);
            // If result is longer than buffer, copy the remainder into cache (this could happen only if placeholder text itself did not fit entirely in buffer)
            if (numBytesCopied < pvlen) {
                cache_.insert(cache_.begin(), pvstr + numBytesCopied, pvstr + pvlen);
            } else if (pTemplateStart + numBytesCopied < pTemplateEnd + 1) { // result is copied fully; if result is shorter than placeholder text...
                // there is some free room, fill it from cache
                const size_t roomFreed = pTemplateEnd + 1 - pTemplateStart - numBytesCopied;
                const size_t totalFreeRoom = originalLen - len + roomFreed;
                len += readDataFromCacheOrContent(&data[len - roomFreed], totalFreeRoom) - roomFreed;
            } else { // result is copied fully; it is longer than placeholder text
                const size_t roomTaken = pTemplateStart + numBytesCopied - pTemplateEnd - 1;
                len = std::min(len + roomTaken, originalLen);
            }
        }
    }
    return len;
}

