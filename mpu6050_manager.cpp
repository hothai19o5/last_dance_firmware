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
      stepCount_(0), lastStepMs_(0), minStepIntervalMs_(600), stepThreshold_(0.55f) {}

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
 * @brief Cập nhật trạng thái cảm biến và phát hiện bước chân
 *
 * Quá trình:
 * 1. Đọc gia tốc 3 chiều từ MPU6050
 * 2. Tính độ lớn gia tốc (magnitude)
 * 3. Áp dụng bộ lọc high-pass để loại bỏ trọng lực
 * 4. Phát hiện bước khi:
 *    - High-pass filtered magnitude > ngưỡng
 *    - Khoảng thời gian từ bước trước > minStepIntervalMs (để tránh nhiễu)
 * 5. Tăng bộ đếm bước
 *
 * Gọi hàm này với tần suất 50-100 Hz để có độ chính xác tốt.
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
            lastStepMs_ = now;
        }
        rising = false;
    }

    prevHp = hp;
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
