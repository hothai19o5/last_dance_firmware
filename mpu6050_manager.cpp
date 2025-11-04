#include "mpu6050_manager.h"
#include <math.h>

// MPU6050 Registers
static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
static constexpr uint8_t REG_SMPLRT_DIV = 0x19;
static constexpr uint8_t REG_CONFIG = 0x1A;
static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;

MPU6050Manager::MPU6050Manager()
    : wire_(nullptr), addr_(0x68), ax_(0), ay_(0), az_(0),
      mag_g_(0.0f), prevRawMag_(0.0f), hpVal_(0.0f), alphaHP_(0.9f),
      stepCount_(0), lastStepMs_(0), minStepIntervalMs_(300), stepThreshold_(0.18f) {}

bool MPU6050Manager::begin(TwoWire &wire, uint8_t address)
{
    wire_ = &wire;
    addr_ = address;

    // Wake up device
    if (!writeReg(REG_PWR_MGMT_1, 0x00))
        return false;
    delay(50);
    // Set DLPF to ~44 Hz (CONFIG=3)
    if (!writeReg(REG_CONFIG, 0x03))
        return false;
    // Set accelerometer range to +/-2g
    if (!writeReg(REG_ACCEL_CONFIG, 0x00))
        return false;
    // Sample rate divider: with DLPF on, gyro rate = 1kHz; 1k/(1+9) = 100 Hz
    if (!writeReg(REG_SMPLRT_DIV, 9))
        return false;

    // Prime readings
    readAccel();
    float m = sqrtf((float)ax_ * ax_ + (float)ay_ * ay_ + (float)az_ * az_);
    prevRawMag_ = m / 16384.0f; // LSB/g for +/-2g
    hpVal_ = 0.0f;

    return true;
}

void MPU6050Manager::update()
{
    if (!wire_)
        return;

    readAccel();
    // Magnitude in g
    float m = sqrtf((float)ax_ * ax_ + (float)ay_ * ay_ + (float)az_ * az_);
    mag_g_ = m / 16384.0f;

    // High-pass filter to remove gravity
    float hp = highPass(mag_g_);
    hpVal_ = hp;

    // Simple peak detection with refractory period
    uint32_t now = millis();
    if (hp > stepThreshold_ && (now - lastStepMs_) > minStepIntervalMs_)
    {
        stepCount_++;
        lastStepMs_ = now;
    }
}

uint32_t MPU6050Manager::getStepCount() const { return stepCount_; }
float MPU6050Manager::getAccelMagnitudeG() const { return mag_g_; }

bool MPU6050Manager::writeReg(uint8_t reg, uint8_t val)
{
    if (!wire_)
        return false;
    wire_->beginTransmission(addr_);
    wire_->write(reg);
    wire_->write(val);
    return (wire_->endTransmission() == 0);
}

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

void MPU6050Manager::readAccel()
{
    uint8_t buf[6];
    if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf)))
    {
        return;
    }
    ax_ = (int16_t)((buf[0] << 8) | buf[1]);
    ay_ = (int16_t)((buf[2] << 8) | buf[3]);
    az_ = (int16_t)((buf[4] << 8) | buf[5]);
}

float MPU6050Manager::highPass(float x)
{
    // One-pole HPF: y[n] = a*(y[n-1] + x[n] - x[n-1])
    float y = alphaHP_ * (hpVal_ + x - prevRawMag_);
    prevRawMag_ = x;
    return y;
}
