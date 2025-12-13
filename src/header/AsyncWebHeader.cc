#include "AsyncWebHeader.h"
#include <string>

AsyncWebHeader::AsyncWebHeader(const std::string& name, const std::string& value)
    : name_(name)
    , value_(value)
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
