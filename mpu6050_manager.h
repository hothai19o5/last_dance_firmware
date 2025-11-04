#pragma once
#include <Arduino.h>
#include <Wire.h>

class MPU6050Manager
{
public:
    MPU6050Manager();
    // Initialize MPU6050 on the given I2C bus; default address 0x68
    bool begin(TwoWire &wire, uint8_t address = 0x68);
    // Read sensor and update step count (call ~50-100 Hz)
    void update();
    // Total steps detected since boot/reset
    uint32_t getStepCount() const;
    // Current acceleration magnitude in g (approximate)
    float getAccelMagnitudeG() const;

private:
    bool writeReg(uint8_t reg, uint8_t val);
    bool readRegs(uint8_t reg, uint8_t *buf, size_t len);
    void readAccel();
    float highPass(float x);

    TwoWire *wire_;
    uint8_t addr_;

    int16_t ax_, ay_, az_;
    float mag_g_;      // |accel| in g
    float prevRawMag_; // previous raw magnitude
    float hpVal_;      // high-pass filtered magnitude
    float alphaHP_;    // high-pass filter coefficient

    uint32_t stepCount_;
    uint32_t lastStepMs_;
    uint16_t minStepIntervalMs_;
    float stepThreshold_; // threshold on HP output to detect a step
};
