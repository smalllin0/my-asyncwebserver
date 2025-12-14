#ifndef ASYNCWEBHEADER_H_
#define ASYNCWEBHEADER_H_

#include <string>

/// HTTP头部信息（如 Content-Type、Cookie、User-Agent 等）
class AsyncWebHeader {
public:
    AsyncWebHeader(std::string name, std::string value);
    AsyncWebHeader(const  std::string& data);
    const std::string& name() const {
        return name_;
    }
    const std::string& value() const {
        return value_;
    }
    std::string toString() const {
        return name_ + ": " + value_ + "\r\n";
    }
private:
    std::string     name_;
    std::string     value_;
};

#endif