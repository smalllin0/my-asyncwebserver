#ifndef ASYNCWEBSOCKETRESPONSE_H_
#define ASYNCWEBSOCKETRESPONSE_H_

#include "AsyncWebServerResponse.h"

class AsyncWebSocket;

class AsyncWebSocketResponse : public AsyncWebServerResponse {
public:
    AsyncWebSocketResponse(const std::string &key, AsyncWebSocket* server);
    void respond(AsyncWebServerRequest* req);
    virtual size_t ack(AsyncWebServerRequest* req, size_t len, uint32_t time) override;
    bool sourceValid() const { return true; }

private:
    std::string     content_;
    AsyncWebSocket* socket_;
};

#endif