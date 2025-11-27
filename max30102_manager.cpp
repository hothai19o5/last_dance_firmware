/**
 * @file max30102_manager.cpp
 * @brief Triển khai quản lý cảm biến MAX30102
 */

#include "max30102_manager.h"
#include <Arduino.h>

/**
 * @brief Constructor - khởi tạo các biến thành viên
 *
 * - rateSpot = 0: vị trí đầu tiên trong bộ đệm rates
 * - lastBeat = 0: chưa phát hiện nhịp tim nào
 * - currentHR = 0.0: nhịp tim chưa được đo
 * - currentSPO2 = 98.0: giá trị mặc định
 * - sensorStatus = 1: ban đầu là lỗi (chưa khởi tạo)
 */
SensorManager::SensorManager()
    : rateSpot(0), lastBeat(0), currentHR(0.0), currentSPO2(98.0), sensorStatus(1)
{
    // Khởi tạo bộ đệm nhịp tim với giá trị 0
    for (byte i = 0; i < RATE_SIZE; i++)
    {
        rates[i] = 0;
    }
}

/**
 * @brief Khởi tạo cảm biến MAX30102 trên bus I2C được chỉ định
 *
 * Quá trình khởi tạo:
 * 1. Bắt đầu Wire1 với chân SDA, SCL được chỉ định
 * 2. Kiểm tra xem cảm biến có phản hồi không
 * 3. Cấu hình các tham số cảm biến (LED IR, độ nhạy)
 * 4. Đợi cảm biến sẵn sàng
 *
 * @param sda Chân SDA của I2C
 * @param scl Chân SCL của I2C
 */
void SensorManager::begin(int sda, int scl)
{
    // Khởi tạo bus I2C riêng cho cảm biến MAX30102 (Wire1)
    Wire1.begin(sda, scl);
    delay(100);

    // Kiểm tra xem cảm biến có thể truy cập được không
    if (!particleSensor.begin(Wire1, I2C_SPEED_FAST))
    {
        Serial.println("MAX30102 not found!");
        while (1)
            ; // Dừng nếu không tìm thấy cảm biến
    }

    Serial.println("MAX30102 initialized.");

    // Cấu hình cảm biến với các tham số cơ bản
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A); // Cường độ LED đỏ (không dùng)
    particleSensor.setPulseAmplitudeGreen(0);  // Không dùng
    particleSensor.setPulseAmplitudeIR(0x33);  // Cường độ LED hồng ngoại (để đo)

    delay(500);
    Serial.println("MAX30102 ready. Place your finger on sensor.");
}

/**
 * @brief Đọc dữ liệu từ cảm biến MAX30102 và cập nhật nhịp tim, SpO2
 *
 * Quy trình:
 * 1. Đọc giá trị IR từ cảm biến
 * 2. Kiểm tra xem ngón tay có trên cảm biến không (IR > 50000)
 * 3. Phát hiện nhịp tim từ tín hiệu IR
 * 4. Tính toán nhịp tim trung bình từ 4 lần phát hiện gần đây
 * 5. Ước tính SpO2 dựa trên nhịp tim
 *
 * Ghi chú: Công thức SpO2 là ước tính đơn giản, không phải đo chính xác
 */
void SensorManager::readSensorData()
{
    long irValue = particleSensor.getIR();

    // Debug thông tin mỗi 2 giây
    static unsigned long lastDebugTime = 0;
    static int beatDetectCount = 0;
    static int totalReadings = 0;

    totalReadings++;

    // In ra thống kê mỗi 2 giây
    if (millis() - lastDebugTime > 2000)
    {
        Serial.printf("[Sensor] IR=%ld, Beat detects in last 2s: %d, Total reads: %d\n",
                      irValue, beatDetectCount, totalReadings);
        beatDetectCount = 0;
        totalReadings = 0;
        lastDebugTime = millis();
    }

    // Kiểm tra xem ngón tay có trên cảm biến không
    // Nếu IR quá thấp, có thể ngón tay chưa được đặt lên cảm biến
    if (irValue < 50000)
    {
        sensorStatus = 1; // Lỗi: IR quá thấp
        Serial.println("[Sensor] WARNING: IR value too low (finger not on sensor?)");
        return;
    }

    // Phát hiện nhịp tim từ tín hiệu IR
    if (checkForBeat(irValue) == true)
    {
        beatDetectCount++;
        Serial.println("[Sensor] BEAT DETECTED!");

        // Tính toán khoảng thời gian giữa hai nhịp tim
        long delta = millis() - lastBeat;
        lastBeat = millis();

        // Chuyển đổi khoảng thời gian thành BPM (Beats Per Minute)
        float beatsPerMinute = 60.0 / (delta / 1000.0);
        Serial.printf("[Sensor] BPM calculated: %.1f\n", beatsPerMinute);

        // Kiểm tra xem BPM có hợp lệ không (20-255 BPM)
        if (beatsPerMinute < 255 && beatsPerMinute > 20)
        {
            // Thêm vào bộ đệm nhịp tim (lưu 4 lần gần đây)
            rates[rateSpot++] = (byte)beatsPerMinute;
            rateSpot %= RATE_SIZE; // Quay vòng: 0,1,2,3,0,1,2,3...

            // Tính trung bình cộng của 4 giá trị BPM gần đây
            int beatAvg = 0;
            for (byte x = 0; x < RATE_SIZE; x++)
            {
                beatAvg += rates[x];
            }
            beatAvg /= RATE_SIZE;

            currentHR = (float)beatAvg;

            // Ước tính SpO2 dựa trên nhịp tim (công thức đơn giản)
            // Công thức: SpO2 = 95 + (100 - BPM) / 10
            currentSPO2 = 95.0 + (100.0 - beatAvg) / 10.0;

            // Giới hạn SpO2 trong phạm vi 80-100%
            if (currentSPO2 > 100)
                currentSPO2 = 100;
            if (currentSPO2 < 80)
                currentSPO2 = 80;

            sensorStatus = 0; // Dữ liệu hợp lệ
            Serial.printf("[Sensor] HR=%.0f, SPO2=%.0f, IR=%ld\n", currentHR, currentSPO2, irValue);
        }
        else
        {
            // BPM ngoài phạm vi hợp lệ
            Serial.printf("[Sensor] BPM out of range: %.1f\n", beatsPerMinute);
        }
    }
}

/**
 * @brief Kiểm tra xem dữ liệu cảm biến hiện tại có hợp lệ không
 * @return true nếu sensorStatus == 0 (dữ liệu hợp lệ), false nếu sensorStatus == 1
 */
bool SensorManager::hasValidData()
{
    return (sensorStatus == 0);
}

/**
 * @brief Lấy dữ liệu cảm biến hiện tại
 * @return Cấu trúc SensorData chứa nhịp tim (HR) và độ bão hòa oxy (SpO2)
 */
SensorData SensorManager::getCurrentData()
{
    SensorData data;
    data.hr = currentHR;
    data.spo2 = currentSPO2;
    return data;
}

/**
 * @brief Lấy tham chiếu đến hồ sơ người dùng
 * @return Tham chiếu UserProfile hiện tại (để có thể sửa đổi)
 */
UserProfile &SensorManager::getUserProfile()
{
    return currentUser;
}
