#include "AsyncWebRewrite.h"
#include "../request/AsyncWebServerRequest.h"
#include <string>

AsyncWebRewrite::AsyncWebRewrite(const char* from, const char* to)
    : from_(from)
    , to_(to)
    , filter_(nullptr)
{
    auto index = to_.find('?');
    if (index != std::string::npos) {
        params_ = to_.substr(index + 1);
        to_ = to_.substr(0, index);
    }
}
