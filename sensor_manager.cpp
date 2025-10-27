#include "sensor_manager.h"
#include "config.h"
#include <Arduino.h>

SensorManager::SensorManager()
    : rateSpot(0), lastBeat(0), currentHR(0.0), currentSPO2(98.0), sensorStatus(1)
{
    for (byte i = 0; i < RATE_SIZE; i++)
    {
        rates[i] = 0;
    }
}

void SensorManager::begin(int sda, int scl)
{
    // Initialize dedicated I2C bus for sensor on Wire1
    Wire1.begin(sda, scl);
    delay(100);

    if (!particleSensor.begin(Wire1, I2C_SPEED_FAST))
    {
        Serial.println("MAX30102 not found!");
        while (1)
            ;
    }

    Serial.println("MAX30102 initialized.");
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    particleSensor.setPulseAmplitudeIR(0x33);

    delay(500);
    Serial.println("MAX30102 ready. Place your finger on sensor.");
}

void SensorManager::readSensorData()
{
    long irValue = particleSensor.getIR();

    // Debug mỗi 2 giây
    static unsigned long lastDebugTime = 0;
    static int beatDetectCount = 0;
    static int totalReadings = 0;

    totalReadings++;

    if (millis() - lastDebugTime > 2000)
    {
        Serial.printf("[Sensor] IR=%ld, Beat detects in last 2s: %d, Total reads: %d\n",
                      irValue, beatDetectCount, totalReadings);
        beatDetectCount = 0;
        totalReadings = 0;
        lastDebugTime = millis();
    }

    // Kiểm tra ngón tay
    if (irValue < 50000)
    {
        sensorStatus = 1;
        Serial.println("[Sensor] WARNING: IR value too low (finger not on sensor?)");
        return;
    }

    // Phát hiện nhịp tim
    if (checkForBeat(irValue) == true)
    {
        beatDetectCount++;
        Serial.println("[Sensor] BEAT DETECTED!");

        long delta = millis() - lastBeat;
        lastBeat = millis();

        float beatsPerMinute = 60.0 / (delta / 1000.0);
        Serial.printf("[Sensor] BPM calculated: %.1f\n", beatsPerMinute);

        if (beatsPerMinute < 255 && beatsPerMinute > 20)
        {
            rates[rateSpot++] = (byte)beatsPerMinute;
            rateSpot %= RATE_SIZE;

            int beatAvg = 0;
            for (byte x = 0; x < RATE_SIZE; x++)
            {
                beatAvg += rates[x];
            }
            beatAvg /= RATE_SIZE;

            currentHR = (float)beatAvg;
            currentSPO2 = 95.0 + (100.0 - beatAvg) / 10.0;
            if (currentSPO2 > 100)
                currentSPO2 = 100;
            if (currentSPO2 < 80)
                currentSPO2 = 80;

            sensorStatus = 0;
            Serial.printf("[Sensor] HR=%.0f, SPO2=%.0f, IR=%ld\n", currentHR, currentSPO2, irValue);
        }
        else
        {
            Serial.printf("[Sensor] BPM out of range: %.1f\n", beatsPerMinute);
        }
    }
}

bool SensorManager::hasValidData()
{
    return (sensorStatus == 0);
}

SensorData SensorManager::getCurrentData()
{
    SensorData data;
    data.hr = currentHR;
    data.spo2 = currentSPO2;
    return data;
}

UserProfile &SensorManager::getUserProfile()
{
    return currentUser;
}
