#ifndef ASYNCSENDFRAME_H_
#define ASYNCSENDFRAME_H_

extern size_t webSocketSendFrame(AsyncClient* client, bool final, uint8_t opcode, bool mask, uint8_t* data, size_t len);

#endif