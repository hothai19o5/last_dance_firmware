/**
 * @file ble_service_manager.cpp
 * @brief Triển khai quản lý dịch vụ BLE
 * @author Hồ Xuân Thái
 * @date 2025
 */

#include "ble_service_manager.h"
#include <sys/time.h>
#include <time.h>

/**
 * @brief Constructor - khởi tạo các biến thành viên và giá trị mặc định
 */
BLEServiceManager::BLEServiceManager()
    : pServer_(nullptr), pUserProfileService_(nullptr), pHealthDataService_(nullptr),
      pBatteryService_(nullptr), pBmiChar_(nullptr), pStepCountEnabledChar_(nullptr),
      pHealthDataBatchChar_(nullptr), pMLEnabledChar_(nullptr), pBatteryLevelChar_(nullptr),
      pTimeSyncChar_(nullptr), pDataTransmissionModeChar_(nullptr),
      clientConnected_(false), stepCountEnabled_(true), mlEnabled_(true),
      dataTransmissionMode_(MODE_REALTIME), lastActivityMs_(0)
{
    // Khởi tạo hồ sơ người dùng mặc định
    userProfile_.bmi = 25.003625;
}

/**
 * @brief Khởi tạo BLE Server với hai dịch vụ
 *
 * Quy trình:
 * 1. Khởi tạo thiết bị BLE với tên được chỉ định
 * 2. Tạo BLE Server
 * 3. Tạo User Profile Service với 1 Characteristic (BMI)
 * 4. Tạo Health Data Service với 1 Characteristic (dữ liệu sức khỏe)
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

    // Characteristic: Chỉ số khối cơ thể (BMI) (READ + WRITE)
    pBmiChar_ = pUserProfileService_->createCharacteristic(
        BMI_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pBmiChar_->setCallbacks(this);
    float defaultBmi = userProfile_.bmi;
    pBmiChar_->setValue((uint8_t *)&defaultBmi, sizeof(float));

    // Characteristic: Bật/tắt đếm bước (READ + WRITE)
    pStepCountEnabledChar_ = pUserProfileService_->createCharacteristic(
        STEP_COUNT_ENABLED_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pStepCountEnabledChar_->setCallbacks(this);
    uint8_t defaultStepEnabled = stepCountEnabled_ ? 1 : 0;
    pStepCountEnabledChar_->setValue(&defaultStepEnabled, 1);

    // Characteristic: Bật/tắt ML (READ + WRITE)
    pMLEnabledChar_ = pUserProfileService_->createCharacteristic(
        ML_ENABLED_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pMLEnabledChar_->setCallbacks(this);
    uint8_t defaultMLEnabled = mlEnabled_ ? 1 : 0;
    pMLEnabledChar_->setValue(&defaultMLEnabled, 1);

    // Characteristic: Đồng bộ thời gian (WRITE)
    pTimeSyncChar_ = pUserProfileService_->createCharacteristic(
        TIME_SYNC_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    pTimeSyncChar_->setCallbacks(this);

    // Characteristic: Chế độ truyền dữ liệu (READ + WRITE)
    pDataTransmissionModeChar_ = pUserProfileService_->createCharacteristic(
        DATA_TRANSMISSION_MODE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
    pDataTransmissionModeChar_->setCallbacks(this);
    uint8_t defaultMode = (uint8_t)dataTransmissionMode_;
    pDataTransmissionModeChar_->setValue(&defaultMode, 1);

    pUserProfileService_->start();

    // === Tạo Health Data Service ===
    pHealthDataService_ = pServer_->createService(HEALTH_DATA_SERVICE_UUID);

    // Characteristic: Dữ liệu sức khỏe (NOTIFY)
    // Ứng dụng di động sẽ nhận thông báo khi dữ liệu thay đổi
    pHealthDataBatchChar_ = pHealthDataService_->createCharacteristic(
        HEALTH_DATA_BATCH_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    pHealthDataBatchChar_->addDescriptor(new BLE2902());

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

/**
 * @brief Callback được gọi khi ứng dụng di động ghi dữ liệu vào một Characteristic
 *
 * Xử lý cập nhật hồ sơ người dùng từ ứng dụng:
 * - BMI
 * - Bật/tắt đếm bước
 * - Bật/tắt ML
 * - Đồng bộ thời gian hệ thống
 *
 * @param pCharacteristic Con trỏ đến Characteristic được ghi
 */
void BLEServiceManager::onWrite(BLECharacteristic *pCharacteristic)
{
    lastActivityMs_ = millis(); // Cập nhật thời điểm hoạt động cuối cùng

    std::string uuid = pCharacteristic->getUUID().toString().c_str();

    // Cập nhật BMI
    if (uuid == BMI_CHAR_UUID)
    {
        float bmi = *(float *)pCharacteristic->getData();
        userProfile_.bmi = bmi;
        Serial.printf("[BLE] Updated BMI: %.2f\n", bmi);
    }
    // Cập nhật bật/tắt đếm bước
    else if (uuid == STEP_COUNT_ENABLED_CHAR_UUID)
    {
        uint8_t enabled = *(uint8_t *)pCharacteristic->getData();
        stepCountEnabled_ = (enabled != 0);
        Serial.printf("[BLE] Step count enabled: %s\n", stepCountEnabled_ ? "YES" : "NO");
    }
    // Cập nhật bật/tắt ML
    else if (uuid == ML_ENABLED_CHAR_UUID)
    {
        uint8_t enabled = *(uint8_t *)pCharacteristic->getData();
        mlEnabled_ = (enabled != 0);
        Serial.printf("[BLE] ML enabled: %s\n", mlEnabled_ ? "YES" : "NO");
    }
    // Cập nhật thời gian hệ thống
    else if (uuid == TIME_SYNC_CHAR_UUID)
    {
        if (pCharacteristic->getLength() >= 4)
        {
            uint32_t timestamp = *(uint32_t *)pCharacteristic->getData();
            struct timeval tv;
            tv.tv_sec = timestamp;
            tv.tv_usec = 0;
            settimeofday(&tv, NULL);

            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            Serial.printf("[BLE] Time synced: %02d:%02d:%02d %02d/%02d/%04d (TS: %u)\n",
                          t->tm_hour, t->tm_min, t->tm_sec,
                          t->tm_mday, t->tm_mon + 1, t->tm_year + 1900,
                          timestamp);
        }
    }
    // Cập nhật chế độ truyền dữ liệu
    else if (uuid == DATA_TRANSMISSION_MODE_CHAR_UUID)
    {
        uint8_t mode = *(uint8_t *)pCharacteristic->getData();
        if (mode == 0)
        {
            dataTransmissionMode_ = MODE_REALTIME;
            Serial.println("[BLE] Mode switched to REALTIME");
        }
        else if (mode == 1)
        {
            dataTransmissionMode_ = MODE_BATCH;
            Serial.println("[BLE] Mode switched to BATCH");
        }
    }
}

/**
 * @brief Gửi dữ liệu sức khỏe hiện tại đến ứng dụng di động (Binary)
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

    HealthDataPacket packet;
    packet.hr = (uint8_t)hr;
    packet.spo2 = (uint8_t)spo2;
    packet.steps = steps;
    
    time_t now;
    time(&now);
    packet.timestamp = (uint32_t)now;

    // Cập nhật giá trị của Characteristic (10 bytes)
    pHealthDataBatchChar_->setValue((uint8_t *)&packet, sizeof(packet));
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified binary data: HR=%d, SpO2=%d, Steps=%d, TS=%u\n",
                  packet.hr, packet.spo2, packet.steps, packet.timestamp);
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

    // Packet structure: [HealthDataPacket (10 bytes)] + [AlertScore (4 bytes)]
    // Total: 14 bytes
    
    uint8_t buffer[sizeof(HealthDataPacket) + sizeof(float)];
    
    HealthDataPacket *packet = (HealthDataPacket *)buffer;
    packet->hr = (uint8_t)hr;
    packet->spo2 = (uint8_t)spo2;
    packet->steps = steps;
    
    time_t now;
    time(&now);
    packet->timestamp = (uint32_t)now;

    // Copy alert score float to the end of buffer
    memcpy(buffer + sizeof(HealthDataPacket), &alertScore, sizeof(float));

    // Cập nhật giá trị của Characteristic
    pHealthDataBatchChar_->setValue(buffer, sizeof(buffer));

    // Gửi thông báo đến ứng dụng
    pHealthDataBatchChar_->notify();

    Serial.printf("[BLE] Notified binary data WITH ALERT: Score=%.4f\n", alertScore);
}

/**
 * @brief Gửi batch dữ liệu HR/SpO2 qua BLE (Binary Array)
 */
bool BLEServiceManager::notifyHealthDataBatch(uint8_t *data, size_t len)
{
    if (!clientConnected_)
    {
        Serial.println("[BLE] Cannot send batch - not connected");
        return false;
    }

    Serial.printf("[BLE] Sending binary batch data: %d bytes\n", len);

    // Gửi toàn bộ dữ liệu một lần bằng uint8_t array
    // setValue với uint8_t* và length sẽ gửi toàn bộ data
    pHealthDataBatchChar_->setValue(data, len);
    pHealthDataBatchChar_->notify();

    lastActivityMs_ = millis();
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

/**
 * @brief Kiểm tra xem ML có được bật không
 * @return true nếu ML được bật
 */
bool BLEServiceManager::isMLEnabled() const
{
    return mlEnabled_;
}

DataTransmissionMode BLEServiceManager::getDataTransmissionMode() const
{
    return dataTransmissionMode_;
}