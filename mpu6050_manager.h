/**
 * @file mpu6050_manager.h
 * @brief Quản lý cảm biến gia tốc kế MPU6050 để đếm bước chân, phát hiện hoạt động và giấc ngủ
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Chức năng:
 * - Đọc gia tốc 3 chiều từ MPU6050
 * - Tính độ lớn gia tốc (acceleration magnitude)
 * - Áp dụng bộ lọc high-pass để loại bỏ trọng lực
 * - Phát hiện các bước chân dựa trên ngưỡng
 * - Phân loại hoạt động: Ngủ, Nghỉ, Đi bộ, Chạy bộ
 * - Theo dõi thời gian ngủ với reset hàng ngày
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

// Enum cho trạng thái hoạt động
enum ActivityStatus : uint8_t
{
    ACTIVITY_SLEEPING = 0, ///< Đang ngủ (low movement + HR thấp)
    ACTIVITY_RESTING = 1,  ///< Đang nghỉ (no steps)
    ACTIVITY_WALKING = 2,  ///< Đang đi bộ (<2 steps/sec)
    ACTIVITY_RUNNING = 3   ///< Đang chạy bộ (>2.5 steps/sec)
};

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

    /// @brief Reset số bước về 0 (dùng khi qua ngày mới)
    void resetStepCount();

    /// @brief Lấy độ lớn gia tốc hiện tại
    /// @return Độ lớn gia tốc tính bằng g (gravitational acceleration)
    float getAccelMagnitudeG() const;

    /// @brief Lấy trạng thái hoạt động hiện tại
    /// @return ActivityStatus (0=sleeping, 1=resting, 2=walking, 3=running)
    ActivityStatus getActivityStatus() const;

    /// @brief Lấy thời gian ngủ tích lũy (phút)
    /// @return Số phút đã ngủ kể từ nửa đêm
    uint16_t getSleepDurationMinutes() const;

    /// @brief Reset thời gian ngủ về 0 (gọi mỗi nửa đêm)
    void resetSleepDuration();

    /// @brief Cập nhật phát hiện giấc ngủ với thông tin nhịp tim
    /// @param heartRate Nhịp tim hiện tại (BPM)
    void updateSleepDetection(uint8_t heartRate);

    /// @brief Đặt MPU6050 vào chế độ sleep để tiết kiệm năng lượng
    /// Gọi khi tắt chức năng đếm bước
    void sleep();

    /// @brief Đánh thức MPU6050 từ chế độ sleep
    /// Gọi khi bật lại chức năng đếm bước
    void wake();

    /// @brief Kiểm tra xem MPU6050 có đang ở chế độ sleep không
    /// @return true nếu đang sleep, false nếu đang hoạt động
    bool isAsleep() const;

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

    // Activity classification và sleep tracking
    ActivityStatus activityStatus_;    ///< Trạng thái hoạt động hiện tại
    uint32_t stepsSinceLastCheck_;     ///< Số bước từ lần check cuối
    uint32_t lastActivityCheckMs_;     ///< Thời điểm check hoạt động lần cuối
    uint16_t activityCheckIntervalMs_; ///< Khoảng thời gian giữa các lần check (1000ms = 1 sec)

    // Sleep tracking
    uint32_t sleepDurationSeconds_;                                      ///< Tổng thời gian ngủ (giây)
    uint32_t sleepStartMs_;                                              ///< Thời điểm bắt đầu ngủ (0 = không ngủ)
    bool isSleeping_;                                                    ///< Trạng thái hiện tại có đang ngủ không
    uint32_t lowActivityDurationMs_;                                     ///< Thời gian liên tục ít hoạt động
    static constexpr uint32_t SLEEP_DETECT_THRESHOLD_MS = 5 * 60 * 1000; ///< 5 phút không hoạt động

    bool isSensorAsleep_; ///< Trạng thái sleep của cảm biến (tiết kiệm năng lượng)
};
