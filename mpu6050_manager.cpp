/**
 * @file mpu6050_manager.cpp
 * @brief Triển khai quản lý cảm biến gia tốc MPU6050
 */

#include "mpu6050_manager.h"
#include <math.h>

// Các thanh ghi quan trọng của MPU6050
static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;   ///< Quản lý năng lượng
static constexpr uint8_t REG_SMPLRT_DIV = 0x19;   ///< Bộ chia tần suất lấy mẫu
static constexpr uint8_t REG_CONFIG = 0x1A;       ///< Cấu hình DLPF (Digital Low Pass Filter)
static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C; ///< Cấu hình gia tốc kế (phạm vi)
static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B; ///< Byte cao của X acceleration

/**
 * @brief Constructor - khởi tạo các biến với giá trị mặc định
 */
MPU6050Manager::MPU6050Manager()
    : wire_(nullptr), addr_(0x68), ax_(0), ay_(0), az_(0),
      mag_g_(0.0f), prevRawMag_(0.0f), hpVal_(0.0f), alphaHP_(0.97f),
      stepCount_(0), lastStepMs_(0), minStepIntervalMs_(600), stepThreshold_(0.55f),
      activityStatus_(ACTIVITY_RESTING), stepsSinceLastCheck_(0),
      lastActivityCheckMs_(0), activityCheckIntervalMs_(1000),
      sleepDurationSeconds_(0), sleepStartMs_(0), isSleeping_(false),
      lowActivityDurationMs_(0), isSensorAsleep_(false) {}

/**
 * @brief Khởi tạo MPU6050 trên bus I2C được chỉ định
 *
 * Quá trình khởi tạo:
 * 1. Bật cảm biến (thoát chế độ sleep)
 * 2. Cấu hình bộ lọc low-pass số (DLPF) để ~44 Hz
 * 3. Đặt phạm vi gia tốc kế ±2g
 * 4. Đặt tần suất lấy mẫu 100 Hz
 * 5. Đọc lần đầu để khởi tạo bộ lọc high-pass
 *
 * @param wire Tham chiếu đến bus I2C
 * @param address Địa chỉ I2C của MPU6050 (mặc định 0x68)
 * @return true nếu khởi tạo thành công
 */
bool MPU6050Manager::begin(TwoWire &wire, uint8_t address)
{
    wire_ = &wire;
    addr_ = address;

    // Bật cảm biến (thoát chế độ sleep bằng cách ghi 0 vào PWR_MGMT_1)
    if (!writeReg(REG_PWR_MGMT_1, 0x00))
        return false;
    delay(50);

    // Cấu hình DLPF: CONFIG=3 → tần số cắt ~44 Hz
    if (!writeReg(REG_CONFIG, 0x03))
        return false;

    // Cấu hình phạm vi gia tốc: 0x00 = ±2g (LSB = 16384 LSB/g)
    if (!writeReg(REG_ACCEL_CONFIG, 0x00))
        return false;

    // Tần suất lấy mẫu: SMPLRT_DIV=9 → 1000/(1+9) = 100 Hz
    if (!writeReg(REG_SMPLRT_DIV, 9))
        return false;

    // Đọc lần đầu để khởi tạo bộ lọc high-pass
    readAccel();
    float m = sqrtf((float)ax_ * ax_ + (float)ay_ * ay_ + (float)az_ * az_);
    prevRawMag_ = m / 16384.0f; // Chuyển đổi từ thô sang g
    hpVal_ = 0.0f;

    return true;
}

/**
 * @brief Lấy tổng số bước đã phát hiện
 * @return Số bước từ khi khởi động hoặc reset
 */
uint32_t MPU6050Manager::getStepCount() const { return stepCount_; }

/**
 * @brief Reset số bước về 0
 */
void MPU6050Manager::resetStepCount()
{
    stepCount_ = 0;
    // Không reset lastStepMs_ để tránh double count ngay lập tức
}

/**
 * @brief Lấy độ lớn gia tốc hiện tại
 * @return Độ lớn gia tốc tính bằng g (9.81 m/s²)
 */
float MPU6050Manager::getAccelMagnitudeG() const { return mag_g_; }

/**
 * @brief Ghi một byte vào thanh ghi I2C của MPU6050
 * @param reg Số thanh ghi
 * @param val Giá trị cần ghi
 * @return true nếu thành công
 */
bool MPU6050Manager::writeReg(uint8_t reg, uint8_t val)
{
    if (!wire_)
        return false;
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(val);
    return (wire_->endTransmission() == 0);
}

/**
 * @brief Đọc nhiều byte từ thanh ghi I2C của MPU6050
 * @param reg Số thanh ghi bắt đầu
 * @param buf Con trỏ đến bộ đệm để lưu dữ liệu
 * @param len Số byte cần đọc
 * @return true nếu đọc thành công
 */
bool MPU6050Manager::readRegs(uint8_t reg, uint8_t *buf, size_t len)
{
    if (!wire_)
        return false;
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    if (wire_->endTransmission(false) != 0)
        return false;
    size_t n = wire_->requestFrom((int)addr_, (int)len);
    if (n != len)
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        buf[i] = wire_->read();
    }
    return true;
}

/**
 * @brief Đọc gia tốc 3 chiều từ MPU6050
 *
 * Lưu vào: ax_, ay_, az_ (dưới dạng thô int16)
 */
void MPU6050Manager::readAccel()
{
    uint8_t buf[6];
    if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf)))
    {
        return;
    }
    // Tập hợp 2 byte (High byte + Low byte) thành int16
    ax_ = (int16_t)((buf[0] << 8) | buf[1]);
    ay_ = (int16_t)((buf[2] << 8) | buf[3]);
    az_ = (int16_t)((buf[4] << 8) | buf[5]);
}

/**
 * @brief Áp dụng bộ lọc high-pass one-pole
 *
 * Công thức: y[n] = a * (y[n-1] + x[n] - x[n-1])
 * Tác dụng: Loại bỏ trọng lực DC và các tần số thấp
 *
 * @param x Tín hiệu đầu vào hiện tại
 * @return Tín hiệu đã lọc high-pass
 */
float MPU6050Manager::highPass(float x)
{
    // Công thức high-pass one-pole: y[n] = a*(y[n-1] + x[n] - x[n-1])
    // với alphaHP_ = 0.9 (loại bỏ mạnh tần số thấp)
    float y = alphaHP_ * (hpVal_ + x - prevRawMag_);
    prevRawMag_ = x; // Lưu lại giá trị hiện tại cho lần tiếp theo
    return y;
}

/**
 * @brief Lấy trạng thái hoạt động hiện tại
 * @return ActivityStatus enum
 */
ActivityStatus MPU6050Manager::getActivityStatus() const
{
    return activityStatus_;
}

/**
 * @brief Phân loại hoạt động dựa trên tần suất bước chân
 *
 * Logic:
 * - Sleeping: isSleeping_ == true (được set bởi updateSleepDetection)
 * - Resting: 0 steps trong 1 giây
 * - Walking: < 2 steps/sec
 * - Running: >= 2.5 steps/sec
 *
 * Gọi hàm này mỗi giây để cập nhật
 */
void MPU6050Manager::update()
{
    if (!wire_)
        return;

    // Đọc gia tốc thô từ cảm biến
    readAccel();

    // Tính độ lớn gia tốc: |a| = sqrt(ax^2 + ay^2 + az^2)
    float m = sqrtf((float)ax_ * ax_ + (float)ay_ * ay_ + (float)az_ * az_);
    mag_g_ = m / 16384.0f; // Chuyển đổi từ thô sang g

    // Lọc high-pass để loại bỏ trọng lực (phần tử DC)
    float hp = highPass(mag_g_);
    hpVal_ = hp;

    // Phát hiện đỉnh (peak) - bất kỳ khi nào HP magnitude vượt ngưỡng
    // và đủ thời gian đã trôi qua kể từ bước cuối cùng (tránh nhiễu)
    static float prevHp = 0;
    static bool rising = false;

    uint32_t now = millis();

    // Phát hiện sườn lên
    if (hp > prevHp && hp > 0)
    {
        rising = true;
    }

    // Phát hiện đỉnh thật sự (peak)
    if (rising && hp < prevHp)
    {
        if (prevHp > stepThreshold_ && (now - lastStepMs_) > minStepIntervalMs_)
        {
            stepCount_++;
            stepsSinceLastCheck_++;
            lastStepMs_ = now;

            // Log mỗi 10 bước để không spam quá nhiều
            if (stepCount_ % 10 == 0)
            {
                Serial.printf("[MPU] Step detected! Total: %u steps\n", stepCount_);
            }

            // Reset low activity timer nếu có bước chân
            lowActivityDurationMs_ = 0;
        }
        rising = false;
    }

    prevHp = hp;

    // Activity classification - check mỗi 1 giây
    if ((now - lastActivityCheckMs_) >= activityCheckIntervalMs_)
    {
        uint32_t elapsedSec = (now - lastActivityCheckMs_) / 1000;
        if (elapsedSec == 0)
            elapsedSec = 1; // Tránh chia cho 0

        // Tính steps per second
        float stepsPerSec = (float)stepsSinceLastCheck_ / (float)elapsedSec;

        // Lưu trạng thái cũ để so sánh
        ActivityStatus oldStatus = activityStatus_;

        // Phân loại hoạt động
        if (isSleeping_)
        {
            activityStatus_ = ACTIVITY_SLEEPING;
        }
        else if (stepsSinceLastCheck_ == 0)
        {
            activityStatus_ = ACTIVITY_RESTING;
            // Tăng low activity timer
            lowActivityDurationMs_ += activityCheckIntervalMs_;
        }
        else if (stepsPerSec < 2.0f)
        {
            activityStatus_ = ACTIVITY_WALKING;
            lowActivityDurationMs_ = 0;
        }
        else
        {
            activityStatus_ = ACTIVITY_RUNNING;
            lowActivityDurationMs_ = 0;
        }

        // Log khi có thay đổi trạng thái
        if (oldStatus != activityStatus_)
        {
            const char *statusNames[] = {"SLEEPING", "RESTING", "WALKING", "RUNNING"};
            Serial.printf("[MPU] Activity changed: %s -> %s (%.1f steps/sec)\n",
                          statusNames[oldStatus], statusNames[activityStatus_], stepsPerSec);
        }

        // Reset counter cho lần check tiếp theo
        stepsSinceLastCheck_ = 0;
        lastActivityCheckMs_ = now;
    }
}

/**
 * @brief Cập nhật phát hiện giấc ngủ dựa trên low movement + heart rate
 *
 * Logic phát hiện ngủ:
 * - Low movement (không có bước chân) trong 5 phút liên tục
 * - Heart rate < 60 BPM
 *
 * @param heartRate Nhịp tim hiện tại (BPM)
 */
void MPU6050Manager::updateSleepDetection(uint8_t heartRate)
{
    uint32_t now = millis();

    // Điều kiện phát hiện ngủ: low activity + HR thấp
    bool sleepCondition = (lowActivityDurationMs_ >= SLEEP_DETECT_THRESHOLD_MS) && (heartRate > 0 && heartRate < 60);

    if (sleepCondition && !isSleeping_)
    {
        // Bắt đầu ngủ
        isSleeping_ = true;
        sleepStartMs_ = now;
        Serial.println("[MPU6050] Sleep detected");
    }
    else if (!sleepCondition && isSleeping_)
    {
        // Thức dậy
        isSleeping_ = false;

        // Cộng dồn thời gian ngủ
        if (sleepStartMs_ > 0)
        {
            uint32_t sleepDuration = (now - sleepStartMs_) / 1000; // Convert to seconds
            sleepDurationSeconds_ += sleepDuration;
            Serial.printf("[MPU6050] Wake detected. Slept for %lu seconds\n", sleepDuration);
        }

        sleepStartMs_ = 0;
    }
    else if (isSleeping_ && sleepStartMs_ > 0)
    {
        // Đang ngủ - cập nhật thời gian ngủ hiện tại (không cộng vào tổng cho đến khi thức)
        // Thời gian ngủ sẽ được cộng khi thức dậy
    }
}

/**
 * @brief Lấy tổng thời gian ngủ tính bằng phút
 * @return Số phút đã ngủ (bao gồm cả giấc ngủ hiện tại nếu đang ngủ)
 */
uint16_t MPU6050Manager::getSleepDurationMinutes() const
{
    uint32_t totalSleepSec = sleepDurationSeconds_;

    // Nếu đang ngủ, thêm thời gian ngủ hiện tại
    if (isSleeping_ && sleepStartMs_ > 0)
    {
        uint32_t currentSleepSec = (millis() - sleepStartMs_) / 1000;
        totalSleepSec += currentSleepSec;
    }

    return (uint16_t)(totalSleepSec / 60); // Convert seconds to minutes
}

/**
 * @brief Reset thời gian ngủ về 0 (gọi mỗi nửa đêm)
 */
void MPU6050Manager::resetSleepDuration()
{
    sleepDurationSeconds_ = 0;
    // Không reset sleepStartMs_ nếu đang ngủ, để tiếp tục tính
    Serial.println("[MPU6050] Sleep duration reset");
}

/**
 * @brief Đặt MPU6050 vào chế độ sleep để tiết kiệm năng lượng
 *
 * Ghi bit SLEEP (bit 6) vào thanh ghi PWR_MGMT_1 (0x6B)
 * Gọi khi tắt chức năng đếm bước để giảm tiêu thụ điện
 */
void MPU6050Manager::sleep()
{
    if (isSensorAsleep_)
    {
        Serial.println("[MPU6050] Already in sleep mode");
        return;
    }

    // Ghi 0x40 (bit 6 = SLEEP) vào PWR_MGMT_1
    if (writeReg(REG_PWR_MGMT_1, 0x40))
    {
        isSensorAsleep_ = true;
        Serial.println("[MPU6050] Entered sleep mode (power saving)");
    }
    else
    {
        Serial.println("[MPU6050] Failed to enter sleep mode");
    }
}

/**
 * @brief Đánh thức MPU6050 từ chế độ sleep
 *
 * Ghi 0x00 vào thanh ghi PWR_MGMT_1 để tắt bit SLEEP
 * Gọi khi bật lại chức năng đếm bước
 */
void MPU6050Manager::wake()
{
    if (!isSensorAsleep_)
    {
        Serial.println("[MPU6050] Already awake");
        return;
    }

    // Ghi 0x00 vào PWR_MGMT_1 để thoát sleep
    if (writeReg(REG_PWR_MGMT_1, 0x00))
    {
        delay(50); // Đợi sensor ổn định sau khi thức dậy
        isSensorAsleep_ = false;
        Serial.println("[MPU6050] Woke up from sleep mode");
    }
    else
    {
        Serial.println("[MPU6050] Failed to wake up");
    }
}

/**
 * @brief Kiểm tra xem MPU6050 có đang ở chế độ sleep không
 * @return true nếu đang sleep, false nếu đang hoạt động
 */
bool MPU6050Manager::isAsleep() const
{
    return isSensorAsleep_;
}
