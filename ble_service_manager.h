/**
 * @file ble_service_manager.h
 * @brief Quản lý dịch vụ Bluetooth Low Energy (BLE) để giao tiếp với ứng dụng di động
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Chức năng:
 * - Khởi tạo BLE Server với các dịch vụ (Services) và đặc trưng (Characteristics)
 * - Cung cấp hai dịch vụ chính:
 *   1. User Profile Service: Nhận cấu hình người dùng từ ứng dụng di động
 *   2. Health Data Service: Gửi dữ liệu sức khỏe thực thời đến ứng dụng di động
 * - Xử lý kết nối/ngắt kết nối từ ứng dụng di động
 * - Cập nhật dữ liệu sức khỏe thông qua BLE Notify
 */

#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "max30102_manager.h"

// === UUID của User Profile Service ===
// Dịch vụ này chứa các thông tin cá nhân từ ứng dụng di động
#define USER_PROFILE_SERVICE_UUID "0000181C-0000-1000-8000-00805F9B34FB"
#define WEIGHT_CHAR_UUID "00002A98-0000-1000-8000-00805F9B34FB"             ///< Cân nặng (kg)
#define HEIGHT_CHAR_UUID "00002A8E-0000-1000-8000-00805F9B34FB"             ///< Chiều cao (m)
#define GENDER_CHAR_UUID "00002A8C-0000-1000-8000-00805F9B34FB"             ///< Giới tính (1=nam, 0=nữ)
#define AGE_CHAR_UUID "00002A80-0000-1000-8000-00805F9B34FB"                ///< Tuổi (năm)
#define STEP_COUNT_ENABLED_CHAR_UUID "00002A81-0000-1000-8000-00805F9B34FB" ///< Bật/tắt đếm bước (1=bật, 0=tắt)

// === UUID của Health Data Service ===
// Dịch vụ này cung cấp dữ liệu sức khỏe theo thời gian thực
#define HEALTH_DATA_SERVICE_UUID "0000180D-0000-1000-8000-00805F9B34FB"
#define HEALTH_DATA_BATCH_CHAR_UUID "00002A37-0000-1000-8000-00805F9B34FB" ///< Dữ liệu sức khỏe (JSON)
#define DEVICE_STATUS_CHAR_UUID "00002A19-0000-1000-8001-00805F9B34FB"     ///< Trạng thái thiết bị

// === UUID cho Battery Service ===
#define BATTERY_SERVICE_UUID "0000180F-0000-1000-8000-00805F9B34FB"
#define BATTERY_LEVEL_CHAR_UUID "00002A19-0000-1000-8000-00805F9B34FB"

/**
 * @class BLEServiceManager
 * @brief Quản lý dịch vụ BLE để giao tiếp với ứng dụng di động
 *
 * Hoạt động:
 * 1. Khởi tạo BLE Server với tên thiết bị
 * 2. Tạo hai dịch vụ:
 *    - User Profile Service: Nhận cài đặt từ ứng dụng (cân nặng, chiều cao, tuổi, giới tính)
 *    - Health Data Service: Gửi dữ liệu sức khỏe thực thời (HR, SpO2, bước, calo)
 * 3. Bắt đầu quảng cáo BLE để ứng dụng di động có thể kết nối
 * 4. Xử lý các sự kiện kết nối/ngắt kết nối
 * 5. Gửi dữ liệu thông qua BLE Notify khi ứng dụng đã kết nối
 */
class BLEServiceManager : public BLEServerCallbacks, public BLECharacteristicCallbacks
{
public:
    /// @brief Constructor - khởi tạo các biến thành viên
    BLEServiceManager();

    /// @brief Khởi tạo BLE Server với tên thiết bị
    /// @param deviceName Tên thiết bị BLE (ví dụ: "Last Dance")
    /// @return true nếu khởi tạo thành công
    bool begin(const char *deviceName);

    /// @brief Gửi dữ liệu sức khỏe đến ứng dụng di động
    /// @param hr Nhịp tim (BPM)
    /// @param spo2 Độ bão hòa oxy (%)
    /// @param steps Tổng số bước
    void notifyHealthData(float hr, float spo2, uint32_t steps);

    /// @brief Gửi dữ liệu sức khỏe kèm cảnh báo bất thường
    /// @param hr Nhịp tim (BPM)
    /// @param spo2 Độ bão hòa oxy (%)
    /// @param steps Tổng số bước
    /// @param alertScore Điểm cảnh báo từ mô hình ML (0-1, -1 = không có cảnh báo)
    void notifyHealthDataWithAlert(float hr, float spo2, uint32_t steps, float alertScore);

    /// @brief Gửi batch dữ liệu HR/SpO2
    /// @param jsonData Chuỗi JSON chứa dữ liệu batch
    /// @return true nếu gửi thành công
    bool notifyHealthDataBatch(const char *jsonData, size_t len);

    /// @brief Cập nhật và gửi mức pin
    /// @param batteryPercent Phần trăm pin (0-100)
    void notifyBatteryLevel(uint8_t batteryPercent);

    /// @brief Kiểm tra xem ứng dụng di động có kết nối không
    /// @return true nếu có khách hàng BLE đang kết nối
    bool isClientConnected() const;

    /// @brief Lấy tham chiếu đến hồ sơ người dùng (để cập nhật từ ứng dụng)
    /// @return Tham chiếu UserProfile
    UserProfile &getUserProfile();

    /// @brief Kiểm tra xem đếm bước có được bật không
    /// @return true nếu đếm bước được bật
    bool isStepCountEnabled() const;

private:
    /// @brief Callback được gọi khi ứng dụng kết nối
    void onConnect(BLEServer *pServer) override;

    /// @brief Callback được gọi khi ứng dụng ngắt kết nối
    void onDisconnect(BLEServer *pServer) override;

    /// @brief Callback được gọi khi ứng dụng ghi dữ liệu vào một Characteristic
    /// Xử lý cập nhật hồ sơ người dùng từ ứng dụng
    void onWrite(BLECharacteristic *pCharacteristic) override;

    BLEServer *pServer_; ///< Con trỏ BLE Server
    BLEService *pUserProfileService_;
    BLEService *pHealthDataService_;
    BLEService *pBatteryService_;

    // Các Characteristic của User Profile Service
    BLECharacteristic *pWeightChar_;           ///< Cân nặng
    BLECharacteristic *pHeightChar_;           ///< Chiều cao
    BLECharacteristic *pGenderChar_;           ///< Giới tính
    BLECharacteristic *pAgeChar_;              ///< Tuổi
    BLECharacteristic *pStepCountEnabledChar_; ///< Bật/tắt đếm bước

    // Các Characteristic của Health Data Service
    BLECharacteristic *pHealthDataBatchChar_; ///< Dữ liệu sức khỏe (JSON)
    BLECharacteristic *pDeviceStatusChar_;    ///< Trạng thái thiết bị
    BLECharacteristic *pBatteryLevelChar_;

    bool clientConnected_;    ///< Cờ: ứng dụng di động có kết nối hay không?
    bool stepCountEnabled_;   ///< Cờ: bật/tắt đếm bước chân (default = true)
    UserProfile userProfile_; ///< Hồ sơ người dùng hiện tại
    unsigned long lastActivityMs_;

    /// @brief Tạo chuỗi JSON chứa dữ liệu sức khỏe để gửi qua BLE
    /// @param hr Nhịp tim
    /// @param spo2 Độ bão hòa oxy
    /// @param steps Tổng số bước
    /// @param alertScore Điểm cảnh báo (mặc định -1 = không có cảnh báo)
    /// @return Chuỗi JSON
    String buildHealthDataJSON(float hr, float spo2, uint32_t steps, float alertScore = -1.0);
};
