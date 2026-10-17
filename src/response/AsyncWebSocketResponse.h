// AsyncWebSocketResponse.h
#ifndef ASYNCWEBSOCKETRESPONSE_H_
#define ASYNCWEBSOCKETRESPONSE_H_

#include "AsyncWebServerResponse.h"

class AsyncWebSocket;
class AsyncWebServerRequest;

class AsyncWebSocketResponse : public AsyncWebServerResponse {
public:
    AsyncWebSocketResponse(const std::string& key, AsyncWebSocket* server);
    bool sourceValid() const override { return state_ != RESPONSE_FAILED; }

protected:
    void doRespond() override;   

private:
    AsyncWebSocket* socket_;
};

#endif