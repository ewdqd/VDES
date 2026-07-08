#ifndef VDES_CRC_H
#define VDES_CRC_H

#include <cstdint>
#include <vector>

// VDES协议CRC校验模块
// 符合 ITU-R M.2092 标准:
//   - Link ID 20: CRC-16 (多项式 0x8005, 初始值 0x0000)
//   - 其他 Link ID: CRC-32 (多项式 0x04C11DB7, 初始值 0xFFFFFFFF)

class VdesCRC {
public:
    // 计算 CRC-16 (用于 Link ID 20)
    // 多项式: x^16 + x^15 + x^2 + 1 (0x8005)
    static uint16_t crc16(const uint8_t* data, size_t len) {
        uint16_t crc = 0x0000;
        for (size_t i = 0; i < len; ++i) {
            crc ^= (uint16_t(data[i]) << 8);
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x8000)
                    crc = (crc << 1) ^ 0x8005;
                else
                    crc <<= 1;
            }
        }
        return crc;
    }

    static uint16_t crc16(const std::vector<uint8_t>& data) {
        return crc16(data.data(), data.size());
    }

    // 计算 CRC-32 (用于除 Link ID 20 外的所有链路)
    // 多项式: 0x04C11DB7, 初始值: 0xFFFFFFFF, 输出异或: 0xFFFFFFFF
    static uint32_t crc32(const uint8_t* data, size_t len) {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x80000000)
                    crc = (crc << 1) ^ 0x04C11DB7;
                else
                    crc <<= 1;
            }
        }
        return crc ^ 0xFFFFFFFF;
    }

    static uint32_t crc32(const std::vector<uint8_t>& data) {
        return crc32(data.data(), data.size());
    }

    // 根据 Link ID 选择 CRC 类型并校验
    // 返回 true 表示校验通过
    static bool verify(const uint8_t* data, size_t len, uint8_t linkId) {
        if (len < 2) return false;

        if (linkId == 20) {
            // CRC-16: 最后2字节为校验值
            if (len < 2) return false;
            uint16_t receivedCRC = (uint16_t(data[len-2]) << 8) | data[len-1];
            uint16_t computedCRC = crc16(data, len - 2);
            return receivedCRC == computedCRC;
        } else {
            // CRC-32: 最后4字节为校验值
            if (len < 4) return false;
            uint32_t receivedCRC = (uint32_t(data[len-4]) << 24) |
                                   (uint32_t(data[len-3]) << 16) |
                                   (uint32_t(data[len-2]) << 8)  |
                                    uint32_t(data[len-1]);
            uint32_t computedCRC = crc32(data, len - 4);
            return receivedCRC == computedCRC;
        }
    }

    static bool verify(const std::vector<uint8_t>& data, uint8_t linkId) {
        return verify(data.data(), data.size(), linkId);
    }

    // 追加 CRC 到数据末尾
    static std::vector<uint8_t> appendCRC(const std::vector<uint8_t>& data, uint8_t linkId) {
        std::vector<uint8_t> result = data;
        if (linkId == 20) {
            uint16_t crc = crc16(data);
            result.push_back((crc >> 8) & 0xFF);
            result.push_back(crc & 0xFF);
        } else {
            uint32_t crc = crc32(data);
            result.push_back((crc >> 24) & 0xFF);
            result.push_back((crc >> 16) & 0xFF);
            result.push_back((crc >> 8) & 0xFF);
            result.push_back(crc & 0xFF);
        }
        return result;
    }
};

#endif // VDES_CRC_H
