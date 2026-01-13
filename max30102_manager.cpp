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
Max30102Manager::Max30102Manager()
    : rateSpot(0), lastBeat(0), currentHR(0.0), currentSPO2(98.0), sensorStatus(1)
{
    // Khởi tạo bộ đệm nhịp tim với giá trị 0
    for (byte i = 0; i < RATE_SIZE; i++)
    {
        rates[i] = 0;
    }
}

/**
 * @brief Khởi tạo cảm biến MAX30102 trên bus I2C đã có sẵn (Wire)
 * @param wire Tham chiếu đến đối tượng TwoWire đã khởi tạo
 * @return true nếu khởi tạo thành công, false nếu không tìm thấy cảm biến
 *
 * Lưu ý: ESP32-C3 chỉ có 1 bus I2C phần cứng, do đó sử dụng chung Wire.
 */
bool Max30102Manager::beginOnWire(TwoWire &wire)
{
    // Không cần khởi tạo Wire1 riêng - dùng Wire đã có sẵn
    if (!particleSensor.begin(wire, I2C_SPEED_FAST))
    {
        Serial.println("[MAX30102] ERROR: Sensor not found!");
        sensorStatus = 1;
        return false; // Không treo thiết bị, trả về false
    }

    Serial.println("[MAX30102] Initialized on shared Wire bus.");

    // Cấu hình cảm biến với LED brightness thấp hơn để tránh ADC overflow
    // ledBrightness: 0x1F (31 - vừa phải)
    // sampleAverage: 1 (không average - đọc nhanh nhất)
    // ledMode: 2 (Red+IR)
    // sampleRate: 400 (400 samples/sec)
    // pulseWidth: 118 (ngắn hơn để sample rate cao hơn)
    // adcRange: 4096
    particleSensor.setup(0x1F, 1, 2, 400, 118, 4096);

    // Độ sáng LED vừa phải để tránh saturation
    particleSensor.setPulseAmplitudeRed(0x1F); // 31 (vừa phải)
    particleSensor.setPulseAmplitudeGreen(0);  // Tắt LED xanh
    particleSensor.setPulseAmplitudeIR(0x1F);  // 31 (vừa phải)

    // Xóa FIFO để bắt đầu sạch
    particleSensor.clearFIFO();

    delay(50); // Giảm delay
    Serial.println("[MAX30102] Ready (Fast mode: 400Hz, no averaging).");
    return true;
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
void Max30102Manager::readSensorData()
{
    // Kiểm tra và đọc tất cả samples có sẵn trong FIFO
    particleSensor.check();

    // Debug: Đếm số samples có sẵn
    static unsigned long lastDebugMs = 0;
    static uint32_t sampleCount = 0;
    static uint32_t lowIrCount = 0;
    static uint32_t processedCount = 0;
    static long minIR = 999999;
    static long maxIR = 0;
    static long sumIR = 0;

    // Đọc tất cả samples có sẵn (không chờ)
    while (particleSensor.available())
    {
        long irValue = particleSensor.getIR();
        long redValue = particleSensor.getRed();
        particleSensor.nextSample();
        sampleCount++;

        // Track IR statistics
        if (irValue < minIR)
            minIR = irValue;
        if (irValue > maxIR)
            maxIR = irValue;
        sumIR += irValue;

        // Kiểm tra ADC overflow (giá trị max của 18-bit ADC)
        if (irValue >= 262143)
        {
            // ADC overflow - LED quá sáng hoặc không có ngón tay
            lowIrCount++; // Đếm vào lỗi
            continue;
        }

        // Kiểm tra IR quá thấp (không có ngón tay)
        if (irValue < 10000)
        {
            // IR quá yếu - không có ngón tay hoặc ngón tay không đủ áp lực
            lowIrCount++;
            continue; // Bỏ qua sample này, đọc tiếp
        }

        // Kiểm tra range hợp lệ: 10000 < IR < 200000
        if (irValue > 200000)
        {
            // IR quá mạnh - có thể LED quá sáng
            lowIrCount++;
            continue;
        }

        // Nếu IR đủ mạnh, coi như có ngón tay (ngay cả khi chưa phát hiện beat)
        // sensorStatus sẽ được set về 0 khi có beat hợp lệ
        processedCount++;

        // Phát hiện nhịp tim từ tín hiệu IR
        if (checkForBeat(irValue) == true)
        {
            Serial.printf("[HR] BEAT! IR=%ld, Red=%ld\n", irValue, redValue);

            // Tính toán khoảng thời gian giữa hai nhịp tim
            long delta = millis() - lastBeat;
            lastBeat = millis();

            // Chuyển đổi khoảng thời gian thành BPM
            float beatsPerMinute = 60.0 / (delta / 1000.0);
            Serial.printf("[HR] Delta=%ldms, BPM=%.1f\n", delta, beatsPerMinute);

            // Kiểm tra BPM hợp lệ (20-255 BPM)
            if (beatsPerMinute < 255 && beatsPerMinute > 20)
            {
                rates[rateSpot++] = (byte)beatsPerMinute;
                rateSpot %= RATE_SIZE;

                // Tính trung bình
                int beatAvg = 0;
                for (byte x = 0; x < RATE_SIZE; x++)
                {
                    beatAvg += rates[x];
                }
                beatAvg /= RATE_SIZE;

                currentHR = (float)beatAvg;

                // Tính SpO2 từ tỉ lệ Red/IR (đơn giản hóa)
                if (redValue > 0 && irValue > 0)
                {
                    float ratio = (float)redValue / (float)irValue;
                    // SpO2 ước tính: 110 - 25 * ratio (công thức đơn giản)
                    currentSPO2 = 110.0 - 25.0 * ratio;
                    if (currentSPO2 > 100)
                        currentSPO2 = 100;
                    if (currentSPO2 < 80)
                        currentSPO2 = 80;
                }

                sensorStatus = 0;
                Serial.println("\n========================================");
                Serial.printf("[HR] \u2705 VALID READING\n");
                Serial.printf("[HR]   Heart Rate: %d BPM\n", beatAvg);
                Serial.printf("[HR]   SpO2: %.0f%%\n", currentSPO2);
                Serial.printf("[HR]   Red/IR Ratio: %.2f\n", (float)redValue / (float)irValue);
                Serial.println("========================================\n");
            }
            else
            {
                Serial.printf("[HR] \u274c BPM out of range: %.1f\n", beatsPerMinute);
            }
        }
    }

    // In debug mỗi 1 giây với thông tin IR chi tiết
    if (millis() - lastDebugMs > 1000)
    {
        long avgIR = sampleCount > 0 ? sumIR / sampleCount : 0;

        // Nếu có IR samples được xử lý nhưng không có beat trong 10 giây, reset status
        if (processedCount > 0 && avgIR > 10000)
        {
            // Có tín hiệu IR tốt
            if (millis() - lastBeat > 10000)
            {
                // Không có beat trong 10 giây → có thể ngón tay không đúng vị trí
                sensorStatus = 1;
                Serial.println("[HR-DBG] No beat detected for 10s - check finger placement");
            }
        }
        else if (processedCount == 0)
        {
            // Không có tín hiệu IR → chắc chắn không có ngón tay
            sensorStatus = 1;
        }

        Serial.printf("[HR-DBG] Total: %d, Processed: %d, Rejected: %d, Status: %s, HR=%.0f\n",
                      sampleCount, processedCount, lowIrCount,
                      sensorStatus == 0 ? "OK" : "NO_FINGER",
                      currentHR);
        Serial.printf("[HR-DBG] IR Range: Min=%ld, Max=%ld, Avg=%ld",
                      minIR, maxIR, avgIR);

        // Cảnh báo nếu có overflow
        if (maxIR >= 262143)
        {
            Serial.print(" ADC OVERFLOW! Reduce LED brightness");
        }
        else if (avgIR < 10000)
        {
            Serial.print(" IR too low - no finger detected");
        }
        else if (avgIR > 200000)
        {
            Serial.print(" IR too high - reduce LED brightness");
        }
        else if (processedCount > 0)
        {
            Serial.print(" IR in valid range");
        }
        Serial.println();

        // Reset counters
        sampleCount = 0;
        processedCount = 0;
        lowIrCount = 0;
        minIR = 999999;
        maxIR = 0;
        sumIR = 0;
        lastDebugMs = millis();
    }
}

/**
 * @brief Kiểm tra xem dữ liệu cảm biến hiện tại có hợp lệ không
 * @return true nếu sensorStatus == 0 (dữ liệu hợp lệ), false nếu sensorStatus == 1
 */
bool Max30102Manager::hasValidData()
{
    return (sensorStatus == 0);
}

/**
 * @brief Lấy dữ liệu cảm biến hiện tại
 * @return Cấu trúc Max30102Data chứa nhịp tim (HR) và độ bão hòa oxy (SpO2)
 */
Max30102Data Max30102Manager::getCurrentData()
{
    Max30102Data data;
    data.hr = currentHR;
    data.spo2 = currentSPO2;
    return data;
}

/**
 * @brief Lấy tham chiếu đến hồ sơ người dùng
 * @return Tham chiếu UserProfile hiện tại (để có thể sửa đổi)
 */
UserProfile &Max30102Manager::getUserProfile()
{
    return currentUser;
}
