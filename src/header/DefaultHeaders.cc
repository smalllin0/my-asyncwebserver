#include "DefaultHeaders.h"
#include "AsyncWebHeader.h"

DefaultHeaders::DefaultHeaders()
    : headers_(headers_t([](AsyncWebHeader* h){ delete h; }))
{}

DefaultHeaders& DefaultHeaders::Instance()
{
    static DefaultHeaders instance;
    return instance;
}

inline void DefaultHeaders::addHeader(std::string name, std::string value)
{
    headers_.add(new AsyncWebHeader(std::move(name), std::move(value)));
}

ConstIterator DefaultHeaders::begin() const
{
    return headers_.begin();
}

ConstIterator DefaultHeaders::end() const
{
    return headers_.end();
}