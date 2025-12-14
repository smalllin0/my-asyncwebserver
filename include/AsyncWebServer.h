#ifndef ASYNCWEBSERVER_H_
#define ASYNCWEBSERVER_H_

#include "AsyncServer.h"
#include "../src/StringArray.h"
#include "../src/handler/AsyncCallbackWebHandler.h"
#include "../src/rewrite/AsyncWebRewrite.h"

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncWebHeader;
class AsyncWebParameter;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncStaticWebHandler;
class AsyncCallbackWebHandler;
class AsyncResponseStream;

extern const char page404[] asm("_binary_404_html_start");

class AsyncWebServer {
public:
    AsyncWebServer(uint16_t port);
    ~AsyncWebServer();
    
    void begin() {
        server_.set_nodelay(true);
        server_.begin();
    }
    void end() {
        server_.end();
    }
    void reset() {
        rewrites_.free();
        handlers_.free();
        if (defaultHandler_) {
            defaultHandler_->onRequest(nullptr);
            defaultHandler_->onUpload(nullptr);
            defaultHandler_->onBody(nullptr);
        }
        server_.set_connected_handler(nullptr, nullptr);
        server_.set_clean_handler(nullptr, nullptr);
    }
    void recycleRequest(AsyncWebServerRequest* req);

    AsyncWebRewrite& addRewrite(AsyncWebRewrite* rewrite);
    bool removeRewrite(AsyncWebRewrite* rewrite);
    AsyncWebRewrite& rewrite(const char* from, const char* to); 

    AsyncWebHandler& addHandler(AsyncWebHandler* handler);
    bool removeHandler(AsyncWebHandler* handler);

    AsyncCallbackWebHandler& on(const char* uri, ArRequestHandlerFunction onReq);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq, ArUploadHandlerFunction onUpload);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onReq, ArUploadHandlerFunction onUpload, ArBodyHandlerFunction onBody);

    AsyncStaticWebHandler& serveStatic(const char* uri, const char* path, const char* cache_ctrl);

    void onNotFound(ArRequestHandlerFunction fn);
    void onFileUpload(ArUploadHandlerFunction fn);
    void onRequestBody(ArBodyHandlerFunction fn);


protected:
    friend class AsyncWebServerRequest;

    AsyncWebServerRequest* allocateRequest(AsyncClient* client);
    void internalHandleDisconnect(AsyncWebServerRequest* req);
    void internalAttachHandler(AsyncWebServerRequest* req);
    void internalRewriteRequest(AsyncWebServerRequest* req);

    AsyncServer     server_;                            // 异步TCP服务器
    LinkedList<AsyncWebRewrite*>    rewrites_;          // URL重写规则链
    LinkedList<AsyncWebHandler*>    handlers_;          // 处理器链
    AsyncCallbackWebHandler*        defaultHandler_;    // 默认处理器（处理未被处理器链匹配项）
    std::atomic<AsyncWebServerRequest*> pool_{nullptr}; // 请求池 
};

#endif