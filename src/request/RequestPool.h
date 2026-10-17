#ifndef REQUEST_POOL_H_
#define REQUEST_POOL_H_

#include "AsyncWebServerRequest.h"
#include "ObjPool.h"

using RequestPool = ObjPool<AsyncWebServerRequest>;

#endif