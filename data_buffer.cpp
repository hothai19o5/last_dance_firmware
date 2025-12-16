/**
 * @file data_buffer.cpp
 * @brief Triển khai buffer dữ liệu HR/SpO2
 * @author Hồ Xuân Thái
 * @date 2025
 */

#include "data_buffer.h"
#include <ArduinoJson.h>
#include <time.h>

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
 * @param steps Số bước chân hiện tại
 * @return true nếu buffer đầy sau khi thêm
 */
bool DataBuffer::addSample(float hr, float spo2, uint32_t steps)
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
    sample.steps = steps;
    
    // Sử dụng Unix timestamp thực tế
    time_t now;
    time(&now);
    sample.timestamp = (uint32_t)now;

    // Thêm vào buffer
    buffer_[head_] = sample;
    head_ = (head_ + 1) % HR_BUFFER_SIZE;

    if (count_ < HR_BUFFER_SIZE)
    {
        count_++;
    }

    Serial.printf("[Buffer] Added sample: HR=%d, SpO2=%d, Steps=%u, Count=%d/%d\n",
                  sample.hr, sample.spo2, sample.steps, count_, HR_BUFFER_SIZE);

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
 * 1. Buffer đầy (HR_BUFFER_SIZE samples)
 * 2. Hoặc đã quá DATA_SEND_INTERVAL_MS (mặc định 1 phút) kể từ mẫu đầu tiên
 * 3. Và có ít nhất MIN_SAMPLES_TO_SEND mẫu trong buffer
 */
bool DataBuffer::shouldSend() const
{
    // Cần ít nhất 10 samples để gửi (tránh gửi dữ liệu quá ít)
    const uint16_t MIN_SAMPLES_TO_SEND = 10;

    if (count_ < MIN_SAMPLES_TO_SEND)
        return false;

    // Buffer đầy
    if (isFull())
        return true;

    // Đã quá DATA_SEND_INTERVAL_MS kể từ mẫu đầu tiên
    if (millis() - firstSampleMs_ >= DATA_SEND_INTERVAL_MS)
    {
        Serial.printf("[Buffer] Time to send: %d samples after %lu ms\n",
                      count_, millis() - firstSampleMs_);
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
 * @brief Lấy dữ liệu dạng mảng JSON các object realtime
 *
 * Format JSON:
 * [
 *   {"hr": 75, "spo2": 98, "steps": 1500, "ts": 1700000001},
 *   {"hr": 76, "spo2": 97, "steps": 1500, "ts": 1700000002},
 *   ...
 * ]
 */
size_t DataBuffer::getCompressedData(char *output, size_t maxLen)
{
    // Tạo JSON document
    // Mỗi object khoảng 60-80 bytes. Buffer size 10 -> cần khoảng 1KB.
    // Dùng 4KB cho an toàn nếu sau này tăng buffer size.
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();

    // Duyệt buffer và thêm vào mảng
    uint16_t startIdx = (count_ >= HR_BUFFER_SIZE) ? head_ : 0;
    for (uint16_t i = 0; i < count_; i++)
    {
        uint16_t idx = (startIdx + i) % HR_BUFFER_SIZE;
        HealthSample &s = buffer_[idx];

        JsonObject obj = array.createNestedObject();
        obj["hr"] = s.hr;
        obj["spo2"] = s.spo2;
        obj["steps"] = s.steps;
        obj["ts"] = s.timestamp;
    }

    // Serialize JSON
    size_t len = serializeJson(array, output, maxLen);

    Serial.printf("[Buffer] Generated JSON array with %d samples (%d bytes)\n", count_, len);

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