#ifndef ASYNCWEBPARAMETER_H_
#define ASYNCWEBPARAMETER_H_

#include <string>

/// HTTP请求参数类
class AsyncWebParameter {
public:

    AsyncWebParameter(std::string name, std::string value, bool form=false, bool file=false, size_t size=0);

    const std::string& name() const {
        return name_;
    }
    const std::string& value() const {
        return value_;    
    }
    size_t size() const {
        return size_;
    }
    bool isPost() const {
        return isForm_;
    }
    bool isFile() const {
        return isFile_;
    }

private:
    std::string     name_;      // 参数名
    std::string     value_;     // 参数值
    size_t          size_;      // 文件大小（仅对文件有效）
    bool            isForm_;    // 是否来自POST表单
    bool            isFile_;    // 是否为上传文件
};

#endif