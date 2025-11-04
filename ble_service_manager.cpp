#include "ble_service_manager.h"
#include <ArduinoJson.h>

BLEServiceManager::BLEServiceManager()
    : pServer_(nullptr), pUserProfileService_(nullptr), pHealthDataService_(nullptr),
      pWeightChar_(nullptr), pHeightChar_(nullptr), pGenderChar_(nullptr), pAgeChar_(nullptr),
      pHealthDataBatchChar_(nullptr), pDeviceStatusChar_(nullptr), clientConnected_(false)
{
    // Initialize default user profile
    userProfile_.gender = 1;
    userProfile_.weight = 65.0;
    userProfile_.height = 1.77;
    userProfile_.age = 21;
    userProfile_.bmr = 0.0; // Will be calculated
}

bool BLEServiceManager::begin(const char *deviceName)
{
    Serial.println("[BLE] Initializing BLE...");

    // Initialize BLE Device
    BLEDevice::init(deviceName);

    // Create BLE Server
    pServer_ = BLEDevice::createServer();
    pServer_->setCallbacks(this);

    // === User Profile Service ===
    pUserProfileService_ = pServer_->createService(USER_PROFILE_SERVICE_UUID);

    // Weight Characteristic (WRITE)
    pWeightChar_ = pUserProfileService_->createCharacteristic(
        WEIGHT_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pWeightChar_->setCallbacks(this);
    uint16_t defaultWeight = (uint16_t)(userProfile_.weight);
    pWeightChar_->setValue(defaultWeight);

    // Height Characteristic (WRITE)
    pHeightChar_ = pUserProfileService_->createCharacteristic(
        HEIGHT_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pHeightChar_->setCallbacks(this);
    uint16_t defaultHeightCm = (uint16_t)(userProfile_.height * 100);
    pHeightChar_->setValue(defaultHeightCm);

    // Gender Characteristic (WRITE)
    pGenderChar_ = pUserProfileService_->createCharacteristic(
        GENDER_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pGenderChar_->setCallbacks(this);
    uint8_t defaultGender = (uint8_t)userProfile_.gender;
    pGenderChar_->setValue(&defaultGender, 1);

    // Age Characteristic (WRITE)
    pAgeChar_ = pUserProfileService_->createCharacteristic(
        AGE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pAgeChar_->setCallbacks(this);
    uint8_t defaultAge = (uint8_t)userProfile_.age;
    pAgeChar_->setValue(&defaultAge, 1);

    pUserProfileService_->start();

    // === Health Data Service ===
    pHealthDataService_ = pServer_->createService(HEALTH_DATA_SERVICE_UUID);

    // Health Data Batch Characteristic (NOTIFY)
    pHealthDataBatchChar_ = pHealthDataService_->createCharacteristic(
        HEALTH_DATA_BATCH_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    pHealthDataBatchChar_->addDescriptor(new BLE2902());

    // Device Status Characteristic (READ)
    pDeviceStatusChar_ = pHealthDataService_->createCharacteristic(
        DEVICE_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pDeviceStatusChar_->addDescriptor(new BLE2902());
    uint8_t statusOnline = 1;
    pDeviceStatusChar_->setValue(&statusOnline, 1);

    pHealthDataService_->start();

    // Start advertising
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(USER_PROFILE_SERVICE_UUID);
    pAdvertising->addServiceUUID(HEALTH_DATA_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] BLE initialized and advertising started.");
    Serial.printf("[BLE] Device Name: %s\n", deviceName);

    return true;
}

void BLEServiceManager::onConnect(BLEServer *pServer)
{
    clientConnected_ = true;
    Serial.println("[BLE] Client connected!");
}

void BLEServiceManager::onDisconnect(BLEServer *pServer)
{
    clientConnected_ = false;
    Serial.println("[BLE] Client disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
}

void BLEServiceManager::onWrite(BLECharacteristic *pCharacteristic)
{
    std::string uuid = pCharacteristic->getUUID().toString();

    if (uuid == WEIGHT_CHAR_UUID)
    {
        uint16_t weightKg = *(uint16_t *)pCharacteristic->getData();
        userProfile_.weight = (float)weightKg;
        Serial.printf("[BLE] Weight updated: %.0f kg\n", userProfile_.weight);
    }
    else if (uuid == HEIGHT_CHAR_UUID)
    {
        uint16_t heightCm = *(uint16_t *)pCharacteristic->getData();
        userProfile_.height = (float)heightCm / 100.0;
        Serial.printf("[BLE] Height updated: %.2f m\n", userProfile_.height);
    }
    else if (uuid == GENDER_CHAR_UUID)
    {
        uint8_t gender = *(uint8_t *)pCharacteristic->getData();
        userProfile_.gender = (int)gender;
        Serial.printf("[BLE] Gender updated: %d\n", userProfile_.gender);
    }
    else if (uuid == AGE_CHAR_UUID)
    {
        uint8_t age = *(uint8_t *)pCharacteristic->getData();
        userProfile_.age = (int)age;
        Serial.printf("[BLE] Age updated: %d\n", userProfile_.age);
    }

    // Recalculate BMR after profile update
    // Mifflin-St Jeor Equation
    if (userProfile_.gender == 1) // Male
    {
        userProfile_.bmr = 10 * userProfile_.weight + 6.25 * (userProfile_.height * 100) - 5 * userProfile_.age + 5;
    }
    else // Female
    {
        userProfile_.bmr = 10 * userProfile_.weight + 6.25 * (userProfile_.height * 100) - 5 * userProfile_.age - 161;
    }
    Serial.printf("[BLE] BMR calculated: %.1f kcal/day\n", userProfile_.bmr);
}

void BLEServiceManager::notifyHealthData(float hr, float spo2, uint32_t steps, float calories)
{
    if (!clientConnected_)
    {
        return;
    }

    String jsonData = buildHealthDataJSON(hr, spo2, steps, calories, -1.0);
    pHealthDataBatchChar_->setValue(jsonData.c_str());
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified health data: %s\n", jsonData.c_str());
}

void BLEServiceManager::notifyHealthDataWithAlert(float hr, float spo2, uint32_t steps, float calories, float alertScore)
{
    if (!clientConnected_)
    {
        return;
    }

    String jsonData = buildHealthDataJSON(hr, spo2, steps, calories, alertScore);
    pHealthDataBatchChar_->setValue(jsonData.c_str());
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified health data WITH ALERT: %s\n", jsonData.c_str());
}

bool BLEServiceManager::isClientConnected() const
{
    return clientConnected_;
}

UserProfile &BLEServiceManager::getUserProfile()
{
    return userProfile_;
}

String BLEServiceManager::buildHealthDataJSON(float hr, float spo2, uint32_t steps, float calories, float alertScore)
{
    // Build compact JSON batch for BLE notify
    // Format: {"hr":75,"spo2":98,"steps":1500,"cal":120.5,"ts":1698765432,"alert":0.85}
    StaticJsonDocument<160> doc;
    doc["hr"] = (int)hr;
    doc["spo2"] = (int)spo2;
    doc["steps"] = steps;
    doc["cal"] = round(calories * 10) / 10.0; // 1 decimal
    doc["ts"] = millis() / 1000;              // timestamp in seconds since boot

    // Include alert score if provided (> 0)
    if (alertScore >= 0.0)
    {
        doc["alert"] = round(alertScore * 10000) / 10000.0; // 4 decimals
    }

    String output;
    serializeJson(doc, output);
    return output;
}
