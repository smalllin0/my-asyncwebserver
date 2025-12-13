#include "AsyncWebParameter.h"

AsyncWebParameter::AsyncWebParameter(const std::string& name, const std::string& value, bool form, bool file, size_t size)
    : name_(name)
    , value_(value)
    , size_(size)
    , isForm_(form)
    , isFile_(file)
{}

