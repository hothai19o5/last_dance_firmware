/**
 * @file mpu6050_manager.h
 * @brief Quản lý cảm biến gia tốc kế MPU6050 để đếm bước chân
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Chức năng:
 * - Đọc gia tốc 3 chiều từ MPU6050
 * - Tính độ lớn gia tốc (acceleration magnitude)
 * - Áp dụng bộ lọc high-pass để loại bỏ trọng lực
 * - Phát hiện các bước chân dựa trên ngưỡng
 * - Đếm tổng số bước từ khi khởi động
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

/**
 * @class MPU6050Manager
 * @brief Quản lý cảm biến gia tốc MPU6050 để đếm bước chân
 *
 * Hoạt động:
 * 1. Đọc giá trị gia tốc 3 chiều từ thanh ghi 0x3B-0x40
 * 2. Tính độ lớn gia tốc (magnitude = sqrt(ax^2 + ay^2 + az^2))
 * 3. Áp dụng bộ lọc high-pass để loại bỏ trọng lực
 * 4. Phát hiện đỉnh (peak) khi HP-filtered magnitude > ngưỡng
 * 5. Tránh phát hiện sai lạc bằng cách đặt chu kỳ tối thiểu giữa các bước
 */
class MPU6050Manager
{
public:
    /// @brief Constructor - khởi tạo các biến
    MPU6050Manager();

    /// @brief Khởi tạo MPU6050 trên bus I2C được chỉ định
    /// @param wire Tham chiếu đến đối tượng TwoWire (I2C)
    /// @param address Địa chỉ I2C của MPU6050 (mặc định 0x68)
    /// @return true nếu khởi tạo thành công, false nếu không tìm thấy cảm biến
    bool begin(TwoWire &wire, uint8_t address = 0x68);

    /// @brief Cập nhật trạng thái cảm biến, phát hiện và đếm bước
    /// Gọi hàm này 50-100 lần/giây để có độ chính xác tốt
    void update();

    /// @brief Lấy tổng số bước đã phát hiện
    /// @return Số bước từ khi khởi động hoặc reset lần cuối
    uint32_t getStepCount() const;

    /// @brief Lấy độ lớn gia tốc hiện tại
    /// @return Độ lớn gia tốc tính bằng g (gravitational acceleration)
    float getAccelMagnitudeG() const;

private:
    /// @brief Ghi một giá trị vào thanh ghi I2C của MPU6050
    bool writeReg(uint8_t reg, uint8_t val);

    /// @brief Đọc nhiều byte từ thanh ghi I2C của MPU6050
    bool readRegs(uint8_t reg, uint8_t *buf, size_t len);

    /// @brief Đọc giá trị gia tốc 3 chiều từ MPU6050
    void readAccel();

    /// @brief Áp dụng bộ lọc high-pass one-pole
    /// @param x Tín hiệu đầu vào
    /// @return Tín hiệu đã lọc
    float highPass(float x);

    TwoWire *wire_; ///< Con trỏ đến bus I2C
    uint8_t addr_;  ///< Địa chỉ I2C của MPU6050

    int16_t ax_, ay_, az_; ///< Giá trị gia tốc 3 chiều (thô)
    float mag_g_;          ///< Độ lớn gia tốc tính bằng g
    float prevRawMag_;     ///< Độ lớn gia tốc từ lần đọc trước
    float hpVal_;          ///< Giá trị lọc high-pass
    float alphaHP_;        ///< Hệ số low-pass (0.9 = loại bỏ tần số thấp mạnh)

    uint32_t stepCount_;         ///< Tổng số bước đã phát hiện
    uint32_t lastStepMs_;        ///< Thời điểm (ms) của bước cuối cùng
    uint16_t minStepIntervalMs_; ///< Khoảng thời gian tối thiểu giữa hai bước (ms) để tránh nhiễu
    float stepThreshold_;        ///< Ngưỡng phát hiện bước (trên tín hiệu high-pass)
};
