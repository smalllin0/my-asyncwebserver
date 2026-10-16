#include "AsyncSendFrame.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

#define TAG "SendFrame"

uint8_t* webSocketMakeFrame(bool final, uint8_t opcode, bool mask,
                            const uint8_t* data, size_t len,
                            size_t* out_len)
{
    if (out_len == nullptr) return nullptr;
    *out_len = 0;

    if (len > 0xFFFFFFFF) {
        ESP_LOGE(TAG, "Frame too large");
        return nullptr;
    }

    // 计算帧头长度
    size_t header_len;
    bool extended16 = (len >= 126 && len < 65536);
    bool extended64 = (len >= 65536);

    if (extended64)      header_len = mask ? 14 : 10;
    else if (extended16) header_len = mask ? 8  : 4;
    else                 header_len = mask ? 6  : 2;

    size_t total = header_len + len;
    auto* buf = static_cast<uint8_t*>(malloc(total));
    if (!buf) return nullptr;

    uint8_t* p = buf;

    // 第一个字节：FIN + opcode
    *p++ = (final ? 0x80 : 0x00) | (opcode & 0x0f);

    // 第二个字节 + 扩展长度 + 掩码密钥
    uint8_t mask_key[4] = {0};
    if (mask) {
        mask_key[0] = rand() & 0xff;
        mask_key[1] = rand() & 0xff;
        mask_key[2] = rand() & 0xff;
        mask_key[3] = rand() & 0xff;
    }

    if (extended64) {
        *p++ = (mask ? 0x80 : 0x00) | 127;
        uint64_t l = len;
        for (int i = 7; i >= 0; --i) {
            *p++ = (l >> (i * 8)) & 0xff;
        }
    } else if (extended16) {
        *p++ = (mask ? 0x80 : 0x00) | 126;
        *p++ = (len >> 8) & 0xff;
        *p++ = len & 0xff;
    } else {
        *p++ = (mask ? 0x80 : 0x00) | static_cast<uint8_t>(len);
    }

    if (mask) {
        memcpy(p, mask_key, 4);
        p += 4;
    }

    // 拷贝载荷（启用掩码时逐字节异或）
    if (len > 0) {
        if (mask) {
            for (size_t i = 0; i < len; ++i) {
                *p++ = data[i] ^ mask_key[i & 0x03];
            }
        } else {
            memcpy(p, data, len);
        }
    }

    *out_len = total;
    return buf;
}