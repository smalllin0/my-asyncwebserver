#include "AsyncClient.h"
#include "esp_log.h"

#define TAG "SendFrame"

/// @brief 构建并发送 WebSocket 帧
/// @param client
/// @param final 是否为最后一帧
/// @param opcode WebSocket消息操作码
/// @param mask 消息是否掩码保护
/// @param data 数据指针
/// @param len 计划发送的字节数
/// @return 发送数据的长度
size_t webSocketSendFrame(AsyncClient* client, bool final, uint8_t opcode, bool mask, uint8_t* data, size_t len)
{

    uint8_t     head_len;
    uint16_t    buf[4];
    uint8_t*    pbuf = (uint8_t*)buf;
    
    if (len == 127) {
        ESP_LOGE(TAG, "Max support 4GB data.");
        return 0;
    } else if (len == 126) {
        head_len = mask ? 8 : 4;
    } else {
        head_len = mask ? 6 : 2;
    }

    size_t space = client->get_send_buffer_size();
    if (space >= head_len) {

        auto send_len = std::min(len, space - head_len);
        uint8_t* mask_ptr = nullptr;

        *pbuf = final ? (0x80 | (0x0f & opcode)) : (0x0f & opcode);
        if (send_len < 126) {
            if (mask) {
                *(pbuf + 1) = 0x80 | send_len;
                buf[1] = rand() & 0xffff;
                buf[2] = rand() & 0xffff;
                mask_ptr = pbuf + 2;
            } else {
                *(pbuf + 1) = send_len;
            }
        } else {
            if (mask) {
                *(pbuf + 1) = 0x80 | 126;
                buf[2] = rand();
                mask_ptr = pbuf + 4;
            } else {
                *(pbuf + 1) = 126;
            }
            *(pbuf + 2) = send_len & 0xffff;
        }

        if (client->add((const char *)buf, head_len) != head_len) {
            return 0;
        }

        if (mask) {
            for (size_t i = 0; i < send_len; i++) {
                data[i] ^= mask_ptr[i & 0x03];
            }
        }

        if (send_len == client->write((const char *)data, send_len, TCP_WRITE_FLAG_MORE)) {
            return send_len;
        }
    }

    return 0;
}

