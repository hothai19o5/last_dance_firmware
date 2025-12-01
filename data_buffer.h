/**
 * @file data_buffer.h
 * @brief Buffer lưu trữ dữ liệu HR/SpO2 trước khi gửi qua BLE
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Chức năng:
 * - Lưu trữ dữ liệu HR/SpO2 mỗi giây
 * - Tự động gửi khi buffer đầy hoặc sau 5 phút
 * - Nén dữ liệu để gửi qua BLE
 */

#pragma once
#include <Arduino.h>
#include "board_config.h"

/**
 * @struct HealthSample
 * @brief Cấu trúc lưu 1 mẫu dữ liệu sức khỏe
 */
struct HealthSample
{
    uint8_t hr;         ///< Nhịp tim (BPM) - 1 byte (0-255)
    uint8_t spo2;       ///< SpO2 (%) - 1 byte (0-100)
    uint32_t timestamp; ///< Timestamp (giây từ boot)
};

/**
 * @class DataBuffer
 * @brief Buffer circular để lưu trữ dữ liệu HR/SpO2
 */
class DataBuffer
{
public:
    /// @brief Constructor
    DataBuffer();

    /// @brief Thêm một mẫu dữ liệu vào buffer
    /// @param hr Nhịp tim (BPM)
    /// @param spo2 Độ bão hòa oxy (%)
    /// @return true nếu buffer đầy sau khi thêm
    bool addSample(float hr, float spo2);

    /// @brief Kiểm tra xem buffer có đầy không
    /// @return true nếu buffer đầy
    bool isFull() const;

    /// @brief Kiểm tra xem có nên gửi dữ liệu không
    /// @return true nếu đủ điều kiện gửi (đầy hoặc timeout)
    bool shouldSend() const;

    /// @brief Lấy số lượng mẫu trong buffer
    /// @return Số mẫu hiện có
    uint16_t getCount() const;

    /// @brief Lấy dữ liệu đã nén để gửi qua BLE
    /// @param output Buffer đầu ra (tối thiểu 512 bytes)
    /// @param maxLen Kích thước tối đa của buffer đầu ra
    /// @param steps Số bước chân hiện tại
    /// @return Số bytes đã ghi vào output
    size_t getCompressedData(char *output, size_t maxLen, uint32_t steps);

    /// @brief Xóa buffer sau khi đã gửi
    void clear();

    /// @brief Reset timestamp gửi cuối cùng
    void resetSendTimer();

    /// @brief Lấy mẫu mới nhất
    /// @return Mẫu dữ liệu mới nhất
    HealthSample getLatestSample() const;

private:
    HealthSample buffer_[HR_BUFFER_SIZE]; ///< Buffer lưu trữ
    uint16_t count_;                      ///< Số mẫu hiện có
    uint16_t head_;                       ///< Vị trí ghi tiếp theo
    unsigned long lastSendMs_;            ///< Thời điểm gửi lần cuối
    unsigned long firstSampleMs_;         ///< Thời điểm mẫu đầu tiên
};