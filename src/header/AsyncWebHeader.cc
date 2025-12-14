#include "AsyncWebHeader.h"
#include <string>

/// @brief 推荐构造，用户可显示使用移动语义
AsyncWebHeader::AsyncWebHeader(std::string name, std::string value)
    : name_(std::move(name)), value_(std::move(value))
{}

AsyncWebHeader::AsyncWebHeader(const std::string& data)
    : name_()
    , value_()
{
    if (data.empty()) {
        return;
    }
    
    size_t index = data.find(':');
    if (index == std::string::npos) {
        return;
    }
    name_ = data.substr(0, index);
    value_ = data.substr(index + 2);
}
