/**
 * @file data_buffer.cpp
 * @brief Triển khai buffer dữ liệu HR/SpO2
 * @author Hồ Xuân Thái
 * @date 2025
 */

#include "data_buffer.h"
#include <ArduinoJson.h>

/**
 * @brief Constructor - khởi tạo buffer rỗng
 */
DataBuffer::DataBuffer()
    : count_(0), head_(0), lastSendMs_(0), firstSampleMs_(0)
{
    memset(buffer_, 0, sizeof(buffer_));
}

/**
 * @brief Thêm một mẫu dữ liệu vào buffer
 *
 * @param hr Nhịp tim (BPM) - sẽ được làm tròn và giới hạn 0-255
 * @param spo2 Độ bão hòa oxy (%) - sẽ được làm tròn và giới hạn 0-100
 * @return true nếu buffer đầy sau khi thêm
 */
bool DataBuffer::addSample(float hr, float spo2)
{
    // Ghi nhận thời điểm mẫu đầu tiên
    if (count_ == 0)
    {
        firstSampleMs_ = millis();
    }

    // Tạo mẫu mới
    HealthSample sample;
    sample.hr = (uint8_t)constrain(hr, 0, 255);
    sample.spo2 = (uint8_t)constrain(spo2, 0, 100);
    sample.timestamp = millis() / 1000; // Timestamp tính bằng giây

    // Thêm vào buffer
    buffer_[head_] = sample;
    head_ = (head_ + 1) % HR_BUFFER_SIZE;

    if (count_ < HR_BUFFER_SIZE)
    {
        count_++;
    }

    Serial.printf("[Buffer] Added sample: HR=%d, SpO2=%d, Count=%d/%d\n",
                  sample.hr, sample.spo2, count_, HR_BUFFER_SIZE);

    return isFull();
}

/**
 * @brief Kiểm tra xem buffer có đầy không
 */
bool DataBuffer::isFull() const
{
    return (count_ >= HR_BUFFER_SIZE);
}

/**
 * @brief Kiểm tra xem có nên gửi dữ liệu không
 *
 * Điều kiện gửi:
 * 1. Buffer đầy (300 samples = 5 phút)
 * 2. Hoặc đã quá 5 phút kể từ mẫu đầu tiên
 * 3. Và có ít nhất 1 mẫu trong buffer
 */
bool DataBuffer::shouldSend() const
{
    if (count_ == 0)
        return false;

    // Buffer đầy
    if (isFull())
        return true;

    // Đã quá 5 phút kể từ mẫu đầu tiên
    if (millis() - firstSampleMs_ >= 1000)
    {
        return true;
    }

    return false;
}

/**
 * @brief Lấy số lượng mẫu trong buffer
 */
uint16_t DataBuffer::getCount() const
{
    return count_;
}

/**
 * @brief Lấy dữ liệu đã nén dạng JSON để gửi qua BLE
 *
 * Format JSON:
 * {
 *   "type": "batch",
 *   "count": 300,
 *   "start_ts": 12345,
 *   "interval": 1,
 *   "hr": [75, 76, 74, ...],
 *   "spo2": [98, 97, 98, ...]
 * }
 *
 * Lưu ý: Do giới hạn BLE MTU, có thể cần chia thành nhiều packet
 */
size_t DataBuffer::getCompressedData(char *output, size_t maxLen)
{
    // Tạo JSON document
    // Với 300 samples, cần khoảng 300*2*4 = 2400 bytes cho arrays
    DynamicJsonDocument doc(4096);

    doc["type"] = "batch";
    doc["count"] = count_;
    doc["start_ts"] = buffer_[0].timestamp;
    doc["interval"] = 1; // 1 giây/sample

    // Tạo arrays cho HR và SpO2
    JsonArray hrArray = doc.createNestedArray("hr");
    JsonArray spo2Array = doc.createNestedArray("spo2");

    // Thêm dữ liệu vào arrays
    uint16_t startIdx = (count_ >= HR_BUFFER_SIZE) ? head_ : 0;
    for (uint16_t i = 0; i < count_; i++)
    {
        uint16_t idx = (startIdx + i) % HR_BUFFER_SIZE;
        hrArray.add(buffer_[idx].hr);
        spo2Array.add(buffer_[idx].spo2);
    }

    // Serialize JSON
    size_t len = serializeJson(doc, output, maxLen);

    Serial.printf("[Buffer] Compressed %d samples into %d bytes\n", count_, len);

    return len;
}

/**
 * @brief Xóa buffer sau khi đã gửi thành công
 */
void DataBuffer::clear()
{
    count_ = 0;
    head_ = 0;
    firstSampleMs_ = 0;
    lastSendMs_ = millis();
    Serial.println("[Buffer] Buffer cleared");
}

/**
 * @brief Reset timer gửi
 */
void DataBuffer::resetSendTimer()
{
    lastSendMs_ = millis();
}

/**
 * @brief Lấy mẫu mới nhất trong buffer
 */
HealthSample DataBuffer::getLatestSample() const
{
    if (count_ == 0)
    {
        HealthSample empty = {0, 0, 0};
        return empty;
    }

    uint16_t lastIdx = (head_ == 0) ? (HR_BUFFER_SIZE - 1) : (head_ - 1);
    return buffer_[lastIdx];
}