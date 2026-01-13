/**
 * @file data_buffer.cpp
 * @brief Triển khai buffer dữ liệu HR/SpO2
 * @author Hồ Xuân Thái
 * @date 2025
 */

#include "data_buffer.h"
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
 * @param alertScore Điểm cảnh báo từ ML (0-1)
 * @param activityStatus Trạng thái hoạt động (0-3)
 * @param sleepDurationMin Thời gian ngủ (phút)
 * @return true nếu buffer đầy sau khi thêm
 */
bool DataBuffer::addSample(float hr, float spo2, uint32_t steps, float alertScore, uint8_t activityStatus, uint16_t sleepDurationMin)
{
    // Ghi nhận thời điểm mẫu đầu tiên
    if (count_ == 0)
    {
        firstSampleMs_ = millis();
    }

    // Tạo mẫu mới (18 bytes)
    HealthDataPacket sample;
    sample.hr = (uint8_t)constrain(hr, 0, 255);
    sample.spo2 = (uint8_t)constrain(spo2, 0, 100);
    sample.steps = steps;
    sample.alertScore = alertScore;
    sample.activityStatus = activityStatus;
    sample.sleepDurationMin = sleepDurationMin;
    sample.reserved[0] = 0;
    sample.reserved[1] = 0;
    sample.reserved[2] = 0;

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

    Serial.printf("[Buffer] Added Extended sample: HR=%d, SpO2=%d, Steps=%u, Alert=%.2f, Activity=%d, Sleep=%dmin, Count=%d/%d\n",
                  sample.hr, sample.spo2, sample.steps, sample.alertScore, sample.activityStatus, sample.sleepDurationMin, count_, HR_BUFFER_SIZE);

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
 * @brief Lấy dữ liệu binary để gửi qua BLE
 *
 * Copy các struct HealthDataPacket từ buffer vòng vào output buffer.
 *
 * @param output Buffer đầu ra
 * @param maxLen Kích thước tối đa của buffer đầu ra
 * @return Số bytes đã ghi
 */
size_t DataBuffer::getBinaryData(uint8_t *output, size_t maxLen)
{
    size_t packetSize = sizeof(HealthDataPacket);
    size_t totalSize = count_ * packetSize;

    if (totalSize > maxLen)
    {
        Serial.printf("[Buffer] Output buffer too small! Need %d bytes, have %d\n", totalSize, maxLen);
        return 0;
    }

    // Duyệt buffer và copy vào output
    // Lưu ý buffer là circular, nên cần duyệt từ phần tử cũ nhất
    uint16_t startIdx = (count_ >= HR_BUFFER_SIZE) ? head_ : 0;

    for (uint16_t i = 0; i < count_; i++)
    {
        uint16_t idx = (startIdx + i) % HR_BUFFER_SIZE;
        memcpy(output + (i * packetSize), &buffer_[idx], packetSize);
    }

    Serial.printf("[Buffer] Prepared Extended batch: %d samples (%d bytes, %d bytes/sample)\n", count_, totalSize, packetSize);

    return totalSize;
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
HealthDataPacket DataBuffer::getLatestSample() const
{
    if (count_ == 0)
    {
        HealthDataPacket empty = {0, 0, 0, 0, 0.0f, 0, 0, {0, 0, 0}};
        return empty;
    }

    uint16_t lastIdx = (head_ == 0) ? (HR_BUFFER_SIZE - 1) : (head_ - 1);
    return buffer_[lastIdx];
}