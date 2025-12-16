/**
 * @file power_manager.cpp
 * @brief Triển khai quản lý nguồn điện
 * @author Hồ Xuân Thái
 * @date 2025
 */

#include "power_manager.h"

/**
 * @brief Constructor
 */
PowerManager::PowerManager()
    : lastVoltage_(0.0), lastPercent_(0), lastReadMs_(0)
{
}

/**
 * @brief Khởi tạo ADC để đọc pin
 */
void PowerManager::begin()
{
    // Cấu hình ADC pin
    pinMode(BATTERY_ADC_PIN, INPUT);
    analogReadResolution(12);       // 12-bit resolution (0-4095)
    analogSetAttenuation(ADC_11db); // Cho phép đọc đến ~3.3V

    // Đọc lần đầu
    readBatteryVoltage();
    Serial.printf("[Power] Battery initialized: %.2fV (%d%%)\n", lastVoltage_, lastPercent_);
}

/**
 * @brief Đọc điện áp pin hiện tại
 *
 * ESP32-C3 ADC có độ phân giải 12-bit (0-4095)
 * Với attenuation 11dB, range là 0-3.3V
 * Voltage divider với tỉ lệ 2:1 cho phép đo đến 6.6V
 *
 * @return Điện áp tính bằng Volt
 */
float PowerManager::readBatteryVoltage()
{
    // Đọc nhiều lần và lấy trung bình để giảm nhiễu
    uint32_t adcSum = 0;
    const int numSamples = 10;

    for (int i = 0; i < numSamples; i++)
    {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(100);
    }

    uint32_t adcAvg = adcSum / numSamples;

    // Chuyển đổi ADC sang voltage
    // ADC 12-bit: 0-4095 tương ứng 0-3.3V
    float adcVoltage = (adcAvg / 4095.0) * 3.3;

    // Nhân với tỉ lệ voltage divider để có điện áp thực
    lastVoltage_ = adcVoltage * VOLTAGE_DIVIDER_RATIO;
    lastReadMs_ = millis();

    // Tính phần trăm
    lastPercent_ = getBatteryPercent();

    return lastVoltage_;
}

/**
 * @brief Tính phần trăm pin dựa trên điện áp
 *
 * Sử dụng công thức tuyến tính đơn giản:
 * percent = (voltage - empty) / (full - empty) * 100
 *
 * @return Phần trăm pin (0-100)
 */
uint8_t PowerManager::getBatteryPercent()
{
    // Đọc lại nếu đã quá 10 giây
    if (millis() - lastReadMs_ > 10000)
    {
        readBatteryVoltage();
    }

    // Tính phần trăm
    float percent = (lastVoltage_ - BATTERY_EMPTY_VOLTAGE) /
                    (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE) * 100.0;

    // Giới hạn trong khoảng 0-100
    if (percent > 100)
        percent = 100;
    if (percent < 0)
        percent = 0;

    lastPercent_ = (uint8_t)percent;
    return lastPercent_;
}