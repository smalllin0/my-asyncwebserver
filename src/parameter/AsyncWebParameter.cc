#include "AsyncWebParameter.h"

AsyncWebParameter::AsyncWebParameter(std::string name,std::string value, bool form, bool file, size_t size)
    : name_(std::move(name))
    , value_(std::move(value))
    , size_(size)
    , isForm_(form)
    , isFile_(file)
{}

