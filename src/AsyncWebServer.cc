#include "AsyncWebServer.h"
#include "../src/request/AsyncWebServerRequest.h"
#include "../src/rewrite/AsyncWebRewrite.h"
#include "../src/handler/AsyncWebHandler.h"
#include "../src/handler/AsyncCallbackWebHandler.h"
#include <atomic>

#define TAG "AsyncWebServer"

AsyncWebServer::AsyncWebServer(uint16_t port)
    : server_(port)
    , rewrites_(LinkedList<AsyncWebRewrite*>([](AsyncWebRewrite* rewrite){ delete rewrite; }))
    , handlers_(LinkedList<AsyncWebHandler*>([](AsyncWebHandler* handler){ delete handler; }))
{
    defaultHandler_ = new AsyncCallbackWebHandler();
    if (defaultHandler_ == nullptr) {
        ESP_LOGE(TAG, "创建通用处理器失败");
        return;
    }
    server_.set_connected_handler([](void* server, AsyncClient* client){
            if (client == nullptr) {
                return;
            }
            auto* self = reinterpret_cast<AsyncWebServer*>(server);

            client->set_rx_timeout_second(3);
            auto* req = self->allocateRequest(self, client);
            // auto* req = new AsyncWebServerRequest((AsyncWebServer*)server, client);
            // ESP_LOGW(TAG, "Size of req: %ld", sizeof(AsyncWebServerRequest));
            // ESP_LOGW(TAG, "Size of string: %ld", sizeof(std::string));
            // ESP_LOGW(TAG, "Size of linklist<*>: %ld", sizeof(LinkedList<std::string*>));
            // ESP_LOGW(TAG, "Size of stringarray: %ld", sizeof(StringArray));
            // ESP_LOGW(TAG, "Size of ArDisconnectHandler: %ld", sizeof(ArDisconnectHandler));
            if (req == nullptr) {
                client->close();
            }
        },
        this
    );
    server_.set_clean_handler([](void* arg){
        auto* self = reinterpret_cast<AsyncWebServer*>(arg);
        auto* head = self->pool_.exchange(nullptr);
        if (head != nullptr) {
            auto* current = head->next_;
            while (current) {
                auto* next = current->next_;
                delete current;
                current = next;
            }
            head->next_ = nullptr;
            self->recycleRequest(head);
        }
    }, this);
    
    recycleRequest(new AsyncWebServerRequest());
}

AsyncWebServer::~AsyncWebServer()
{
    reset();
    end();
    if (defaultHandler_) {
        delete defaultHandler_;
    }
}

/// @brief 添加URL重写规则
AsyncWebRewrite& AsyncWebServer::addRewrite(AsyncWebRewrite* rewrite)
{
    rewrites_.add(rewrite);
    return *rewrite;
}

/// @brief 删除URL重写规则
bool AsyncWebServer::removeRewrite(AsyncWebRewrite* rewrite)
{
    return rewrites_.remove(rewrite);
}

AsyncWebRewrite& AsyncWebServer::rewrite(const char* from, const char* to)
{
    return addRewrite(new AsyncWebRewrite(from, to));
}

/// @brief 添加处理器
AsyncWebHandler& AsyncWebServer::addHandler(AsyncWebHandler* handler) 
{
    handlers_.add(handler);
    return *handler;
}

/// @brief 删除处理器
bool AsyncWebServer::removeHandler(AsyncWebHandler* handler) 
{
    return handlers_.remove(handler);
}

/// @brief 注册HTTP路由
/// @param uri 监听路径
/// @param onReq 主请求处理回调
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, ArRequestHandlerFunction onReq)
{
    auto* handler = new AsyncCallbackWebHandler();
    handler->setUri(uri);
    handler->onRequest(onReq);
    addHandler(handler);
    return *handler;
}

/// @brief 注册HTTP路由
/// @param uri 监听URI路径
/// @param method 允许的HTTP方法组合（如GET|POST）
/// @param onReq 主请求处理回调
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq)
{
    auto* handler = new AsyncCallbackWebHandler();
    handler->setUri(uri);
    handler->setMethod(method);
    handler->onRequest(onReq);
    addHandler(handler);
    return *handler;
}

/// @brief 注册HTTP路由
/// @param uri 监听URI路径
/// @param method 允许的HTTP方法组合（如GET|POST）
/// @param onReq 主请求处理回调
/// @param onUpload 文件上传处理回调
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq, ArUploadHandlerFunction onUpload)
{
    auto* handler = new AsyncCallbackWebHandler();
    handler->setUri(uri);
    handler->setMethod(method);
    handler->onRequest(onReq);
    handler->onUpload(onUpload);
    addHandler(handler);
    return *handler;
}

/// @brief 注册HTTP路由
/// @param uri 监听URI路径
/// @param method 允许的HTTP方法组合（如GET|POST）
/// @param onReq 主请求处理回调
/// @param onUpload 文件上传处理回调
/// @param onBody 原始POST body流式处理回调
AsyncCallbackWebHandler& AsyncWebServer::on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq, ArUploadHandlerFunction onUpload, ArBodyHandlerFunction onBody)
{
    auto* handler = new AsyncCallbackWebHandler();
    handler->setUri(uri);
    handler->setMethod(method);
    handler->onRequest(onReq);
    handler->onUpload(onUpload);
    handler->onBody(onBody);
    addHandler(handler);
    return *handler;
}

/// @brief 提供静态文件服务
/// @param uri 监听URI路径
/// @param path 文件系统中的实际目录
/// @param cache_ctrl HTTP响应头中Cache-Contrl的值（如"max-age=3600",可为nullptr）
AsyncStaticWebHandler& AsyncWebServer::serveStatic(const char* uri, const char* path, const char* cache_ctrl)
{
    auto* handler = new AsyncStaticWebHandler(uri, path, cache_ctrl);
    addHandler(handler);
    return *handler;
}

/// @brief 设置未匹配路径时的默认处理器
/// @param fn 默认处理回调
void AsyncWebServer::onNotFound(ArRequestHandlerFunction fn)
{
    defaultHandler_->onRequest(fn);
}

/// @brief 设置无指定文件上传接口匹配时的默认处理回调
void AsyncWebServer::onFileUpload(ArUploadHandlerFunction fn)
{
    defaultHandler_->onUpload(fn);
}

/// @brief 设置未匹配指定路由的原始body的默认POST/PUT请求解析器
void AsyncWebServer::onRequestBody(ArBodyHandlerFunction fn)
{
    defaultHandler_->onBody(fn);
}


/// @brief 检查请求是否匹配重写规则，匹配时进行重写
void AsyncWebServer::internalRewriteRequest(AsyncWebServerRequest* req)
{
    for (const auto& rewrite : rewrites_) {
        if (rewrite->match(req)) {
            req->url_ = rewrite->toUrl();
            auto params = rewrite->params();
            req->addGetParams(params.c_str(), params.c_str() + params.length());
        }
    }
}

///  @brief 为请求绑定合适的处理器
void AsyncWebServer::internalAttachHandler(AsyncWebServerRequest* req)
{
    for (const auto& handler : handlers_) {
        if (handler->filter(req) && handler->canHandle(req)) {
            req->handler_ = handler;
            return;
        }
    }
    req->addInterestingHeader("ANY");
    req->handler_ = defaultHandler_;
}

/// @brief 处理客户端断开连接的情况
void AsyncWebServer::internalHandleDisconnect(AsyncWebServerRequest* req)
{
    if (req != nullptr) {
        req->client_->close();
    }
}


AsyncWebServerRequest* AsyncWebServer::allocateRequest(AsyncWebServer* server, AsyncClient* client)
{
    AsyncWebServerRequest* req;
    AsyncWebServerRequest* expected;
    do {
        expected = pool_.load();
        if (!expected) {
            req = new AsyncWebServerRequest();
            break;
        }
        req = expected;
    } while (!pool_.compare_exchange_weak(expected, req->next_));

    req->init(server, client);
    return req;
}

void AsyncWebServer::recycleRequest(AsyncWebServerRequest* req) {
    req->reset();
    AsyncWebServerRequest* expected;
    do {
        expected = pool_.load();
        req->next_ = expected;
    } while (!pool_.compare_exchange_weak(expected, req));
}
