/**
 * @file ble_service_manager.cpp
 * @brief Triển khai quản lý dịch vụ BLE
 */

#include "ble_service_manager.h"
#include <ArduinoJson.h>

/**
 * @brief Constructor - khởi tạo các biến thành viên và giá trị mặc định
 */
BLEServiceManager::BLEServiceManager()
    : pServer_(nullptr), pUserProfileService_(nullptr), pHealthDataService_(nullptr),
      pBatteryService_(nullptr), pWeightChar_(nullptr), pHeightChar_(nullptr),
      pGenderChar_(nullptr), pAgeChar_(nullptr), pStepCountEnabledChar_(nullptr),
      pHealthDataBatchChar_(nullptr), pDeviceStatusChar_(nullptr), pBatteryLevelChar_(nullptr),
      clientConnected_(false), stepCountEnabled_(true), lastActivityMs_(0)
{
    // Khởi tạo hồ sơ người dùng mặc định
    userProfile_.gender = 1;    // Nam
    userProfile_.weight = 65.0; // 65 kg
    userProfile_.height = 1.77; // 1.77 m
    userProfile_.age = 21;      // 21 tuổi
    userProfile_.bmr = 0.0;     // Sẽ được tính toán sau
}

/**
 * @brief Khởi tạo BLE Server với hai dịch vụ
 *
 * Quy trình:
 * 1. Khởi tạo thiết bị BLE với tên được chỉ định
 * 2. Tạo BLE Server
 * 3. Tạo User Profile Service với 4 Characteristic (cân nặng, chiều cao, giới tính, tuổi)
 * 4. Tạo Health Data Service với 2 Characteristic (dữ liệu sức khỏe, trạng thái thiết bị)
 * 5. Bắt đầu quảng cáo BLE để ứng dụng di động có thể khám phá thiết bị
 *
 * @param deviceName Tên thiết bị BLE (ví dụ: "Last Dance")
 * @return true nếu khởi tạo thành công
 */
bool BLEServiceManager::begin(const char *deviceName)
{
    Serial.println("[BLE] Initializing BLE...");

    // Khởi tạo thiết bị BLE
    BLEDevice::init(deviceName);

    // Tạo BLE Server
    pServer_ = BLEDevice::createServer();
    pServer_->setCallbacks(this);

    // === Tạo User Profile Service ===
    pUserProfileService_ = pServer_->createService(USER_PROFILE_SERVICE_UUID);

    // Characteristic: Cân nặng (WRITE)
    pWeightChar_ = pUserProfileService_->createCharacteristic(
        WEIGHT_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pWeightChar_->setCallbacks(this);
    uint16_t defaultWeight = (uint16_t)(userProfile_.weight);
    pWeightChar_->setValue(defaultWeight);

    // Characteristic: Chiều cao (WRITE)
    pHeightChar_ = pUserProfileService_->createCharacteristic(
        HEIGHT_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pHeightChar_->setCallbacks(this);
    uint16_t defaultHeightCm = (uint16_t)(userProfile_.height * 100); // Lưu tính bằng cm
    pHeightChar_->setValue(defaultHeightCm);

    // Characteristic: Giới tính (WRITE)
    pGenderChar_ = pUserProfileService_->createCharacteristic(
        GENDER_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pGenderChar_->setCallbacks(this);
    uint8_t defaultGender = (uint8_t)userProfile_.gender;
    pGenderChar_->setValue(&defaultGender, 1);

    // Characteristic: Tuổi (WRITE)
    pAgeChar_ = pUserProfileService_->createCharacteristic(
        AGE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pAgeChar_->setCallbacks(this);
    uint8_t defaultAge = (uint8_t)userProfile_.age;
    pAgeChar_->setValue(&defaultAge, 1);

    // Characteristic: Bật/tắt đếm bước (READ + WRITE)
    pStepCountEnabledChar_ = pUserProfileService_->createCharacteristic(
        STEP_COUNT_ENABLED_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pStepCountEnabledChar_->setCallbacks(this);
    uint8_t defaultStepEnabled = stepCountEnabled_ ? 1 : 0;
    pStepCountEnabledChar_->setValue(&defaultStepEnabled, 1);

    pUserProfileService_->start();

    // === Tạo Health Data Service ===
    pHealthDataService_ = pServer_->createService(HEALTH_DATA_SERVICE_UUID);

    // Characteristic: Dữ liệu sức khỏe (NOTIFY)
    // Ứng dụng di động sẽ nhận thông báo khi dữ liệu thay đổi
    pHealthDataBatchChar_ = pHealthDataService_->createCharacteristic(
        HEALTH_DATA_BATCH_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    pHealthDataBatchChar_->addDescriptor(new BLE2902());

    // Characteristic: Trạng thái thiết bị (READ + NOTIFY)
    pDeviceStatusChar_ = pHealthDataService_->createCharacteristic(
        DEVICE_STATUS_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pDeviceStatusChar_->addDescriptor(new BLE2902());
    uint8_t statusOnline = 1; // 1 = thiết bị hoạt động
    pDeviceStatusChar_->setValue(&statusOnline, 1);

    pHealthDataService_->start();

    // === Battery Service ===
    pBatteryService_ = pServer_->createService(BATTERY_SERVICE_UUID);

    pBatteryLevelChar_ = pBatteryService_->createCharacteristic(
        BATTERY_LEVEL_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pBatteryLevelChar_->addDescriptor(new BLE2902());
    uint8_t defaultBattery = 100;
    pBatteryLevelChar_->setValue(&defaultBattery, 1);

    pBatteryService_->start();

    // === Start Advertising BLE ===
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(USER_PROFILE_SERVICE_UUID);
    pAdvertising->addServiceUUID(HEALTH_DATA_SERVICE_UUID);
    pAdvertising->addServiceUUID(BATTERY_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); // Cải thiện tương thích với iPhone
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] BLE initialized and advertising started.");
    Serial.printf("[BLE] Device Name: %s\n", deviceName);

    return true;
}

/**
 * @brief Callback được gọi khi ứng dụng di động kết nối
 *
 * Xử lý:
 * 1. Cập nhật cờ kết nối
 * 2. Tăng MTU (Maximum Transmission Unit) lên 512 bytes
 *    để có thể gửi các dữ liệu JSON dài mà không cần phân chia
 *
 * @param pServer Con trỏ BLE Server
 */
void BLEServiceManager::onConnect(BLEServer *pServer)
{
    clientConnected_ = true;
    Serial.println("[BLE] Client connected!");

    // Tăng MTU lên 512 bytes (mặc định là 23)
    // Điều này cho phép gửi chuỗi JSON dài mà không cần chia nhỏ
    pServer->updatePeerMTU(pServer->getConnId(), 512);
    Serial.println("[BLE] MTU set to 512 bytes");
}

/**
 * @brief Callback được gọi khi ứng dụng di động ngắt kết nối
 *
 * Xử lý:
 * 1. Cập nhật cờ kết nối
 * 2. Bắt đầu quảng cáo lại để ứng dụng khác có thể kết nối
 *
 * @param pServer Con trỏ BLE Server
 */
void BLEServiceManager::onDisconnect(BLEServer *pServer)
{
    clientConnected_ = false;
    Serial.println("[BLE] Client disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
}

void BLEServiceManager::onWrite(BLECharacteristic *pCharacteristic)
{
    lastActivityMs_ = millis(); // Cập nhật thời điểm hoạt động cuối cùng

    std::string uuid = pCharacteristic->getUUID().toString().c_str();

    // Cập nhật cân nặng
    if (uuid == WEIGHT_CHAR_UUID)
    {
        uint16_t weightKg = *(uint16_t *)pCharacteristic->getData();
        userProfile_.weight = (float)weightKg;
        Serial.printf("[BLE] Weight updated: %.0f kg\n", userProfile_.weight);
    }
    // Cập nhật chiều cao (đổi từ cm sang m)
    else if (uuid == HEIGHT_CHAR_UUID)
    {
        uint16_t heightCm = *(uint16_t *)pCharacteristic->getData();
        userProfile_.height = (float)heightCm / 100.0;
        Serial.printf("[BLE] Height updated: %.2f m\n", userProfile_.height);
    }
    // Cập nhật giới tính
    else if (uuid == GENDER_CHAR_UUID)
    {
        uint8_t gender = *(uint8_t *)pCharacteristic->getData();
        userProfile_.gender = (int)gender;
        Serial.printf("[BLE] Gender updated: %d\n", userProfile_.gender);
    }
    // Cập nhật tuổi
    else if (uuid == AGE_CHAR_UUID)
    {
        uint8_t age = *(uint8_t *)pCharacteristic->getData();
        userProfile_.age = (int)age;
        Serial.printf("[BLE] Age updated: %d\n", userProfile_.age);
    }
    // Cập nhật bật/tắt đếm bước
    else if (uuid == STEP_COUNT_ENABLED_CHAR_UUID)
    {
        uint8_t enabled = *(uint8_t *)pCharacteristic->getData();
        stepCountEnabled_ = (enabled != 0);
        Serial.printf("[BLE] Step count enabled: %s\n", stepCountEnabled_ ? "YES" : "NO");
        return; // Không cần tính BMR
    }

    // === Tính toán BMR (Basal Metabolic Rate) bằng công thức Mifflin-St Jeor ===
    // Công thức:
    // - Nam: BMR = 10×Weight + 6.25×Height - 5×Age + 5 (kcal/day)
    // - Nữ: BMR = 10×Weight + 6.25×Height - 5×Age - 161 (kcal/day)
    if (userProfile_.gender == 1) // Nam
    {
        userProfile_.bmr = 10 * userProfile_.weight + 6.25 * (userProfile_.height * 100) - 5 * userProfile_.age + 5;
    }
    else // Nữ
    {
        userProfile_.bmr = 10 * userProfile_.weight + 6.25 * (userProfile_.height * 100) - 5 * userProfile_.age - 161;
    }
    Serial.printf("[BLE] BMR calculated: %.1f kcal/day\n", userProfile_.bmr);
}

/**
 * @brief Gửi dữ liệu sức khỏe hiện tại đến ứng dụng di động
 *
 * @param hr Nhịp tim (BPM)
 * @param spo2 Độ bão hòa oxy (%)
 * @param steps Tổng số bước
 */
void BLEServiceManager::notifyHealthData(float hr, float spo2, uint32_t steps)
{
    // Không gửi nếu ứng dụng chưa kết nối
    if (!clientConnected_)
    {
        return;
    }

    // Xây dựng dữ liệu JSON
    String jsonData = buildHealthDataJSON(hr, spo2, steps, -1.0);

    // Cập nhật giá trị của Characteristic
    pHealthDataBatchChar_->setValue(jsonData.c_str());

    // Gửi thông báo đến ứng dụng
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified health data: %s\n", jsonData.c_str());
}

/**
 * @brief Gửi dữ liệu sức khỏe kèm cảnh báo (nếu có)
 *
 * @param hr Nhịp tim (BPM)
 * @param spo2 Độ bão hòa oxy (%)
 * @param steps Tổng số bước
 * @param alertScore Điểm cảnh báo từ mô hình ML (0-1)
 */
void BLEServiceManager::notifyHealthDataWithAlert(float hr, float spo2, uint32_t steps, float alertScore)
{
    // Không gửi nếu ứng dụng chưa kết nối
    if (!clientConnected_)
    {
        return;
    }

    // Xây dựng dữ liệu JSON (bao gồm alertScore)
    String jsonData = buildHealthDataJSON(hr, spo2, steps, alertScore);

    // Cập nhật giá trị của Characteristic
    pHealthDataBatchChar_->setValue(jsonData.c_str());

    // Gửi thông báo đến ứng dụng
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified health data WITH ALERT: %s\n", jsonData.c_str());
}

/**
 * @brief Gửi batch dữ liệu HR/SpO2 qua BLE
 *
 * Do giới hạn MTU, dữ liệu lớn sẽ được chia thành nhiều packet
 */
bool BLEServiceManager::notifyHealthDataBatch(const char *jsonData, size_t len)
{
    if (!clientConnected_)
    {
        Serial.println("[BLE] Cannot send batch - not connected");
        return false;
    }

    // Gửi toàn bộ dữ liệu (MTU đã được set 512)
    // Nếu dữ liệu lớn hơn, BLE stack sẽ tự động chia
    pHealthDataBatchChar_->setValue((uint8_t *)jsonData, len);
    pHealthDataBatchChar_->notify();

    lastActivityMs_ = millis();
    Serial.printf("[BLE] Sent batch data: %d bytes\n", len);

    return true;
}

/**
 * @brief Cập nhật và gửi mức pin
 */
void BLEServiceManager::notifyBatteryLevel(uint8_t batteryPercent)
{
    pBatteryLevelChar_->setValue(&batteryPercent, 1);

    if (clientConnected_)
    {
        pBatteryLevelChar_->notify();
        lastActivityMs_ = millis();
        Serial.printf("[BLE] Battery level notified: %d%%\n", batteryPercent);
    }
}

/**
 * @brief Kiểm tra xem ứng dụng di động có kết nối không
 * @return true nếu có khách hàng BLE đang kết nối
 */
bool BLEServiceManager::isClientConnected() const
{
    return clientConnected_;
}

/**
 * @brief Lấy tham chiếu đến hồ sơ người dùng
 * @return Tham chiếu UserProfile (có thể sửa đổi)
 */
UserProfile &BLEServiceManager::getUserProfile()
{
    return userProfile_;
}

/**
 * @brief Kiểm tra xem đếm bước có được bật không
 * @return true nếu đếm bước được bật
 */
bool BLEServiceManager::isStepCountEnabled() const
{
    return stepCountEnabled_;
}

String BLEServiceManager::buildHealthDataJSON(float hr, float spo2, uint32_t steps, float alertScore)
{
    // Build compact JSON batch for BLE notify
    // Format: {"hr":75,"spo2":98,"steps":1500,"cal":120.5,"ts":1698765432,"alert":0.85}
    StaticJsonDocument<160> doc;
    doc["hr"] = (int)hr;
    doc["spo2"] = (int)spo2;
    doc["steps"] = steps;
    doc["ts"] = millis() / 1000; // timestamp in seconds since boot

    // Include alert score if provided (> 0)
    if (alertScore >= 0.0)
    {
        doc["alert"] = round(alertScore * 10000) / 10000.0; // 4 decimals
    }

    String output;
    serializeJson(doc, output);
    return output;
}
