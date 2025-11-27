/**
 * @file max30102_manager.h
 * @brief Quản lý cảm biến MAX30102 để đọc nhịp tim (HR) và độ bão hòa oxy (SpO2)
 * @author Ho Xuan Thai
 * @date 2025
 *
 * Mô-đun này cung cấp:
 * - Khởi tạo cảm biến MAX30102
 * - Đọc dữ liệu IR để phát hiện nhịp tim
 * - Tính toán nhịp tim (BPM) và độ bão hòa oxy
 * - Quản lý hồ sơ người dùng (giới tính, cân nặng, chiều cao, tuổi, BMR)
 */

#pragma once
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// Định nghĩa chân I2C cho cảm biến MAX30102 (trên Wire1)
#define I2C_SDA_MAX30102 17 ///< Chân SDA cho cảm biến MAX30102
#define I2C_SCL_MAX30102 18 ///< Chân SCL cho cảm biến MAX30102

/**
 * @struct SensorData
 * @brief Cấu trúc lưu trữ dữ liệu cảm biến (nhịp tim và SpO2)
 */
struct SensorData
{
    float hr;   ///< Nhịp tim tính bằng BPM (Beats Per Minute)
    float spo2; ///< Độ bão hòa oxy tính bằng % (Oxygen Saturation)
};

/**
 * @struct UserProfile
 * @brief Cấu trúc lưu trữ hồ sơ người dùng để tính toán calo và BMI
 */
struct UserProfile
{
    int gender = 1;      ///< Giới tính (1 = nam, 0 = nữ)
    float weight = 65.0; ///< Cân nặng tính bằng kg
    float height = 1.77; ///< Chiều cao tính bằng mét
    int age = 21;        ///< Tuổi (năm)
    float bmr;           ///< Tỷ lệ chuyển hóa cơ bản (Basal Metabolic Rate) tính bằng kcal/ngày
};

/**
 * @class SensorManager
 * @brief Quản lý cảm biến MAX30102 để đọc dữ liệu nhịp tim và SpO2
 *
 * Chức năng chính:
 * - Khởi tạo cảm biến trên bus I2C riêng biệt (Wire1)
 * - Phát hiện nhịp tim từ tín hiệu IR
 * - Tính toán nhịp tim trung bình từ các đợt phát hiện gần đây
 * - Ước tính độ bão hòa oxy dựa trên nhịp tim
 */
class SensorManager
{
public:
    /// @brief Constructor khởi tạo các biến với giá trị mặc định
    SensorManager();

    /// @brief Khởi tạo cảm biến MAX30102 trên bus I2C được chỉ định
    /// @param sda Chân SDA của I2C
    /// @param scl Chân SCL của I2C
    void begin(int sda, int scl);

    /// @brief Đọc dữ liệu từ cảm biến và cập nhật nhịp tim, SpO2
    /// Phải được gọi trong vòng lặp chính để theo dõi liên tục
    void readSensorData();

    /// @brief Kiểm tra xem dữ liệu cảm biến có hợp lệ không
    /// @return true nếu có dữ liệu hợp lệ, false nếu chưa
    bool hasValidData();

    /// @brief Lấy dữ liệu cảm biến hiện tại (HR và SpO2)
    /// @return Cấu trúc SensorData chứa nhịp tim và SpO2
    SensorData getCurrentData();

    /// @brief Lấy tham chiếu đến hồ sơ người dùng
    /// @return Tham chiếu UserProfile hiện tại
    UserProfile &getUserProfile();

private:
    MAX30105 particleSensor; ///< Đối tượng cảm biến MAX30102

    static const byte RATE_SIZE = 4; ///< Kích thước bộ đệm để lưu các đợt nhịp tim gần đây
    byte rates[RATE_SIZE];           ///< Mảng lưu các giá trị BPM gần đây
    byte rateSpot;                   ///< Vị trí hiện tại trong mảng rates
    long lastBeat;                   ///< Thời điểm (ms) của nhịp tim cuối cùng được phát hiện

    float currentHR;               ///< Nhịp tim trung bình hiện tại
    float currentSPO2;             ///< Độ bão hòa oxy ước tính hiện tại
    volatile uint8_t sensorStatus; ///< Trạng thái cảm biến (0 = hợp lệ, 1 = lỗi)

    UserProfile currentUser; ///< Hồ sơ người dùng (giới tính, cân nặng, v.v.)
};
