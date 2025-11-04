#pragma once
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

#define I2C_SDA_MAX30102 17
#define I2C_SCL_MAX30102 18

// Sensor data structure
struct SensorData
{
    float hr;
    float spo2;
};

// User profile structure
struct UserProfile
{
    int gender = 1;
    float weight = 65.0;
    float height = 1.77;
    int age = 21;
    float bmr;
    
};

class SensorManager
{
public:
    SensorManager();
    void begin(int sda, int scl);
    void readSensorData();
    bool hasValidData();
    SensorData getCurrentData();
    UserProfile &getUserProfile();

private:
    MAX30105 particleSensor;

    static const byte RATE_SIZE = 4;
    byte rates[RATE_SIZE];
    byte rateSpot;
    long lastBeat;

    float currentHR;
    float currentSPO2;
    volatile uint8_t sensorStatus;

    UserProfile currentUser;
};
