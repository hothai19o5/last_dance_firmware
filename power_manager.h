/**
 * @file power_manager.h
 * @brief Quản lý nguồn điện và chế độ tiết kiệm pin cho ESP32-C3
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Chức năng:
 * - Đọc mức pin qua ADC
 * - Quản lý chế độ light sleep
 * - Tính toán phần trăm pin
 */

#pragma once
#include <Arduino.h>
#include "board_config.h"

/**
 * @class PowerManager
 * @brief Quản lý nguồn điện và sleep mode
 */
class PowerManager
{
public:
    /// @brief Constructor
    PowerManager();

    /// @brief Khởi tạo ADC để đọc pin
    void begin();

    /// @brief Đọc điện áp pin hiện tại
    /// @return Điện áp tính bằng Volt
    float readBatteryVoltage();

    /// @brief Tính phần trăm pin
    /// @return Phần trăm pin (0-100)
    uint8_t getBatteryPercent();

private:
    float lastVoltage_;        ///< Điện áp đọc được lần cuối
    uint8_t lastPercent_;      ///< Phần trăm pin lần cuối
    unsigned long lastReadMs_; ///< Thời điểm đọc pin lần cuối
};