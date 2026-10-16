#ifndef ASYNCSENDFRAME_H_
#define ASYNCSENDFRAME_H_

#include <cstdint>
#include <cstddef>

class AsyncConnection;

/// @brief 构造一个完整的 WebSocket 帧（含帧头 + 掩码处理后的载荷）
/// @param final  是否最后一帧
/// @param opcode 帧操作码
/// @param mask   是否启用掩码
/// @param data   载荷数据
/// @param len    载荷长度
/// @param out_len 输出：整个帧的字节数（含帧头）
/// @return malloc 出来的帧缓冲；调用方负责 free
/// @note 返回 nullptr 表示参数错误或内存不足
uint8_t* webSocketMakeFrame(bool final, uint8_t opcode, bool mask,
                            const uint8_t* data, size_t len,
                            size_t* out_len);

#endif