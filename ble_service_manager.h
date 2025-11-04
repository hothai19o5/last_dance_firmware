#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "sensor_manager.h"

// BLE Service and Characteristic UUIDs (matching spec)
// User Profile Service
#define USER_PROFILE_SERVICE_UUID "0000181C-0000-1000-8000-00805F9B34FB"
#define WEIGHT_CHAR_UUID "00002A98-0000-1000-8000-00805F9B34FB"
#define HEIGHT_CHAR_UUID "00002A8E-0000-1000-8000-00805F9B34FB"
#define GENDER_CHAR_UUID "00002A8C-0000-1000-8000-00805F9B34FB"
#define AGE_CHAR_UUID "00002A80-0000-1000-8000-00805F9B34FB"

// Health Data Service (custom)
#define HEALTH_DATA_SERVICE_UUID "0000180D-0000-1000-8000-00805F9B34FB"
#define HEALTH_DATA_BATCH_CHAR_UUID "00002A37-0000-1000-8000-00805F9B34FB"
#define DEVICE_STATUS_CHAR_UUID "00002A19-0000-1000-8000-00805F9B34FB"

class BLEServiceManager : public BLEServerCallbacks, public BLECharacteristicCallbacks
{
public:
    BLEServiceManager();
    // Initialize BLE with device name
    bool begin(const char *deviceName);
    // Update health data batch and notify if connected
    void notifyHealthData(float hr, float spo2, uint32_t steps, float calories);
    // Update health data with alert score
    void notifyHealthDataWithAlert(float hr, float spo2, uint32_t steps, float calories, float alertScore);
    // Check if a client is connected
    bool isClientConnected() const;
    // Get reference to user profile for sensor manager
    UserProfile &getUserProfile();

private:
    // BLE Server Callbacks
    void onConnect(BLEServer *pServer) override;
    void onDisconnect(BLEServer *pServer) override;

    // Characteristic Write Callbacks (for User Profile Service)
    void onWrite(BLECharacteristic *pCharacteristic) override;

    BLEServer *pServer_;
    BLEService *pUserProfileService_;
    BLEService *pHealthDataService_;

    BLECharacteristic *pWeightChar_;
    BLECharacteristic *pHeightChar_;
    BLECharacteristic *pGenderChar_;
    BLECharacteristic *pAgeChar_;

    BLECharacteristic *pHealthDataBatchChar_;
    BLECharacteristic *pDeviceStatusChar_;

    bool clientConnected_;
    UserProfile userProfile_;

    // Helper to build JSON batch for notify
    String buildHealthDataJSON(float hr, float spo2, uint32_t steps, float calories, float alertScore = -1.0);
};
