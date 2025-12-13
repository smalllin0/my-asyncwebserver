#ifndef DEFAULT_H_
#define DEFAULT_H_

#include "../StringArray.h"


class AsyncWebHeader;

using headers_t = LinkedList<AsyncWebHeader*>;
using ConstIterator = headers_t::ConstIterator;

class DefaultHeaders {
public:
    static DefaultHeaders& Instance();

    DefaultHeaders(const DefaultHeaders &) = delete;            // 删除拷贝构造
    DefaultHeaders& operator=(const DefaultHeaders &) = delete; // 删除拷贝复值

    inline void addHeader(const std::string& name, const std::string& value);
    ConstIterator begin() const;
    ConstIterator end() const;

private:
    DefaultHeaders();
    
    headers_t   headers_;
};

#endif // !DEFAULT_H_