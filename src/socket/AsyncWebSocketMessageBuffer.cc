#include "AsyncWebSocketMessageBuffer.h"
#include <string.h>

AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer()
    : data_(nullptr)
    , len_(0)
    , count_(0)
    , lock_(false)
{}

AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(size_t size)
    : data_(nullptr)
    , len_(size)
    , count_(0)
    , lock_(false)
{
    if (size > 0) {
        data_ = new uint8_t[size + 1];
        if (data_ != nullptr) {
            data_[size] = 0;
        }
    }
}

AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(uint8_t* data, size_t size)
    : data_(nullptr)
    , len_(size)
    , count_(0)
    , lock_(false)
{
    if (data == nullptr) {
        return;
    } else {
        data_ = new uint8_t[size + 1];
        if (data_ != nullptr) {
            memcpy(data_, data, len_);
            data_[size] = 0;
        }
    }
}

/// @brief 拷贝构造函数，只增加引用，不复制数据
/// @param copy
// AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(const AsyncWebSocketMessageBuffer &copy)
//     : data_(nullptr)
//     , len_(0)
//     , count_(0)
//     , lock_(false)
// {
//     len_ = copy.len_;
//     lock_ = copy.lock_;
//     count_ = 0;

//     if (len_ > 0) {
//         data_ = new uint8_t[len_ + 1];
//         if (data_ != nullptr) {
//             data_[len_] = 0;
//             memcpy(data_, copy.data_, len_);
//         }
//     }/
// }

/// @brief 移动构造函数
/// @param move
AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(AsyncWebSocketMessageBuffer && move)
    : data_(move.data_), len_(move.len_), count_(move.count_), lock_(move.lock_)
{
    if (move.data_ != nullptr) {
        move.data_ = nullptr;
    }
}

AsyncWebSocketMessageBuffer::~AsyncWebSocketMessageBuffer()
{
    if (data_ != nullptr) {
        delete[] data_;
    }
}

bool AsyncWebSocketMessageBuffer::reserve(size_t size)
{
    if (data_ != nullptr) {
        delete[] data_;
        data_ = nullptr;
    }

    data_ = new uint8_t[size + 1];

    if (data_) {
        data_[size] = 0;
        len_ = size;
        return true;
    } else {
        data_ = new uint8_t[len_ + 1];
        if (data_ != nullptr) {
            data_[len_] = 0;
        }
        return false;
    }
    return false;
}