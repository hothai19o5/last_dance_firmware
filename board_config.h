/**
 * @file board_config.h
 * @brief Cấu hình chân cho các board ESP32 khác nhau
 */

#pragma once

// I2C pins cho MAX30102 (Wire - bus chính)
#define I2C_SDA_MAX30102 8
#define I2C_SCL_MAX30102 9

// MPU6050 dùng cùng bus I2C với MAX30102
// (ESP32-C3 chỉ có 1 hardware I2C, dùng software I2C cho bus thứ 2)
#define I2C_SDA_MPU6050 8
#define I2C_SCL_MPU6050 9

// === Battery ADC pin ===
#define BATTERY_ADC_PIN 0 // GPIO0 (ADC1_CH0) - kết nối với voltage divider

// === Buffer và timing ===
#define HR_BUFFER_SIZE 10          // 100 samples = 50 giây (2 sample/giây)
#define HR_SAMPLE_INTERVAL_MS 500   // Đọc HR mỗi 0.5 giây
#define DATA_SEND_INTERVAL_MS 60000 // Gửi dữ liệu mỗi 1 phút (60000ms)

// === Battery voltage thresholds ===
#define BATTERY_FULL_VOLTAGE 4.2  // Voltage khi pin đầy (Li-Po)
#define BATTERY_EMPTY_VOLTAGE 3.0 // Voltage khi pin cạn
#define VOLTAGE_DIVIDER_RATIO 2.0 // Tỉ lệ voltage divider (R1=R2)