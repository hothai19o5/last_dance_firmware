/**
 * @file last_dance.ino
 * @brief Chương trình chính cho thiết bị theo dõi sức khỏe
 * @author Hồ Xuân Thái
 * @date 2025
 *
 * Tính năng:
 * - Đọc HR/SpO2 mỗi 1 giây, lưu vào buffer
 * - Gửi dữ liệu batch qua BLE mỗi 5 phút hoặc khi buffer đầy
 * - Phân tích ML liên tục với dữ liệu HR/SpO2 mới nhất
 * - Theo dõi và gửi mức pin
 * - Đếm bước chân liên tục
 */

#include "board_config.h"
#include "max30102_manager.h"
#include "ml_model.h"
#include <Wire.h>
#include "mpu6050_manager.h"
#include "ble_service_manager.h"
#include "power_manager.h"
#include "data_buffer.h"
#include <time.h>

// === Global Objects ===
Max30102Manager max30102Manager;
MLModel mlModel;
MPU6050Manager mpuManager;
BLEServiceManager bleManager;
PowerManager powerManager;
DataBuffer dataBuffer;

// === Timing variables ===
static unsigned long lastHrReadMs = 0;
static unsigned long lastBatteryReadMs = 0;
static unsigned long lastAlertSentMs = 0;             // Thời điểm gửi alert cuối cùng
static const unsigned long ALERT_COOLDOWN_MS = 30000; // 30 giây cooldown giữa các alert
static bool mlInitialized = false;
static bool max30102Ready = false; // Cờ kiểm tra MAX30102 đã khởi tạo chưa
static bool isSending = false;     // Cờ đang gửi dữ liệu - tránh gửi lặp
static int lastDayProcessed = -1;  // Lưu ngày đã xử lý để reset steps

struct AlertData
{
  float score;
  float hr;
  float spo2;
};

/**
 * @brief Kiểm tra xem đã qua ngày mới chưa để reset số bước và thời gian ngủ
 */
void checkNewDay()
{
  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);

  // Nếu thời gian chưa được set (ví dụ năm 1970), bỏ qua
  if (timeinfo->tm_year < (2020 - 1900))
    return;

  // Lần đầu tiên chạy (sau khi sync time), cập nhật lastDayProcessed
  if (lastDayProcessed == -1)
  {
    lastDayProcessed = timeinfo->tm_mday;
    return;
  }

  // Nếu ngày hiện tại khác ngày đã xử lý -> Qua ngày mới
  if (timeinfo->tm_mday != lastDayProcessed)
  {
    Serial.println("\n========== [SYSTEM] NEW DAY DETECTED ==========");
    Serial.printf("[SYSTEM] Date changed: %d -> %d\n", lastDayProcessed, timeinfo->tm_mday);
    Serial.printf("[SYSTEM] New date: %02d/%02d/%04d\n",
                  timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
    Serial.println("[SYSTEM] Resetting daily counters...");
    mpuManager.resetStepCount();
    mpuManager.resetSleepDuration(); // Reset sleep duration
    lastDayProcessed = timeinfo->tm_mday;
    Serial.println("[SYSTEM] Daily reset complete!");
    Serial.println("===============================================\n");
  }
}

/**
 * @brief Xử lý ML với dữ liệu HR/SpO2 mới nhất từ buffer
 * Context-aware: Chỉ gửi alert khi phù hợp với trạng thái hoạt động
 * @param hr Nhịp tim mới đọc được
 * @param spo2 SpO2 mới đọc được
 */
void processML(float hr, float spo2)
{
  if (!mlInitialized)
  {
    mlModel.setup();
    mlInitialized = true;
  }

  // Chỉ chạy ML khi có dữ liệu hợp lệ
  if (hr <= 0 || spo2 <= 0)
    return;

  Serial.printf("[ML] Processing: HR=%.1f, SPO2=%.1f\n", hr, spo2);

  UserProfile &profile = bleManager.getUserProfile();
  float bmi = profile.bmi;
  float score = mlModel.runInference(hr, spo2, bmi);

  if (score > 0.95) // Alert threshold
  {
    // Context-aware alert filtering
    ActivityStatus activity = mpuManager.getActivityStatus();
    bool shouldAlert = true;

    // Suppress alert during sleep + low HR (normal for sleep)
    if (activity == ACTIVITY_SLEEPING && hr < 60)
    {
      Serial.println("[ML] Alert suppressed: Sleeping + Low HR (normal)");
      shouldAlert = false;
    }
    // Suppress alert during running + high HR (normal for exercise)
    else if (activity == ACTIVITY_RUNNING && hr > 120)
    {
      Serial.println("[ML] Alert suppressed: Running + High HR (normal)");
      shouldAlert = false;
    }

    if (shouldAlert)
    {
      Serial.printf("[ML] ALERT detected: Score=%.4f (Activity=%d, HR=%.1f)\n", score, activity, hr);

      // Kiểm tra cooldown - chỉ gửi alert nếu đã qua 30 giây từ alert trước
      unsigned long now = millis();
      if (now - lastAlertSentMs >= ALERT_COOLDOWN_MS)
      {
        if (bleManager.isClientConnected())
        {
          Serial.println("[ML] ⚠️ Sending ALERT packet (cooldown passed)");
          uint32_t steps = mpuManager.getStepCount();
          uint16_t sleepDuration = mpuManager.getSleepDurationMinutes();
          bleManager.notifyHealthDataExtended(hr, spo2, steps, score,
                                              (uint8_t)activity, sleepDuration);
          lastAlertSentMs = now; // Cập nhật thời gian gửi alert
        }
      }
      else
      {
        unsigned long remaining = ALERT_COOLDOWN_MS - (now - lastAlertSentMs);
        Serial.printf("[ML] Alert suppressed (cooldown: %lu sec remaining)\n", remaining / 1000);
      }
    }
  }
}

/**
 * @brief Gửi dữ liệu batch qua BLE
 */
void sendBatchData()
{
  // Kiểm tra cờ đang gửi - tránh gửi lặp
  if (isSending)
  {
    Serial.println("[Main] Already sending data, skipping...");
    return;
  }

  if (!dataBuffer.shouldSend())
    return;

  if (!bleManager.isClientConnected())
  {
    Serial.println("[Main] Cannot send batch - BLE not connected");
    return;
  }

  // Đặt cờ đang gửi
  isSending = true;

  Serial.println("\n========== [BATCH] PREPARING DATA ==========");
  Serial.printf("[BATCH] Buffer count: %d/%d samples\n",
                dataBuffer.getCount(), HR_BUFFER_SIZE);

  // Chuẩn bị buffer để gửi - cần 600 * 18 = 10800 bytes
  static uint8_t binaryBuffer[12000]; // Static để tránh stack overflow
  size_t len = dataBuffer.getBinaryData(binaryBuffer, sizeof(binaryBuffer));

  if (len > 0)
  {
    Serial.printf("[BATCH] Binary data size: %d bytes\n", len);
    Serial.printf("[BATCH] Sending to BLE...\n");

    if (bleManager.notifyHealthDataBatch(binaryBuffer, len))
    {
      Serial.println("[BATCH] Success! Clearing buffer...");
      dataBuffer.clear();
    }
    else
    {
      Serial.println("[BATCH] Failed to send");
    }
  }
  else
  {
    Serial.println("[BATCH] No data to send");
  }

  Serial.println("============================================\n");

  // Xóa cờ đang gửi
  isSending = false;
}

/**
 * @brief Đọc HR liên tục và lưu vào buffer mỗi giây
 */
void readAndBufferHR()
{
  // Bỏ qua nếu MAX30102 chưa sẵn sàng
  if (!max30102Ready)
    return;

  // Đọc dữ liệu từ cảm biến liên tục (mỗi vòng loop)
  max30102Manager.readSensorData();

  // Chỉ lưu vào buffer mỗi 1 giây
  if (millis() - lastHrReadMs < HR_SAMPLE_INTERVAL_MS)
    return;
  lastHrReadMs = millis();

  if (max30102Manager.hasValidData())
  {
    Max30102Data data = max30102Manager.getCurrentData();

    // Chạy ML với dữ liệu mới nhất (chỉ chạy nếu được enable)
    if (bleManager.isMLEnabled())
    {
      processML(data.hr, data.spo2);
    }

    // Lấy thông tin chung cho cả 2 mode
    uint32_t steps = mpuManager.getStepCount();
    ActivityStatus activity = mpuManager.getActivityStatus();
    uint16_t sleepDuration = mpuManager.getSleepDurationMinutes();
    float alertScore = 0.0f;

    // Xử lý gửi dữ liệu dựa trên chế độ
    DataTransmissionMode mode = bleManager.getDataTransmissionMode();

    if (mode == MODE_REALTIME)
    {
      // Chế độ Realtime: Gửi ngay lập tức với extended data
      if (bleManager.isClientConnected())
      {
        Serial.printf("[REALTIME] Sending: HR=%.0f, SpO2=%.0f, Steps=%u, Activity=%d\n",
                      data.hr, data.spo2, steps, (int)activity);

        // Send extended packet (18 bytes)
        bleManager.notifyHealthDataExtended(data.hr, data.spo2, steps, alertScore,
                                            (uint8_t)activity, sleepDuration);
      }
    }
    else // MODE_BATCH
    {
      // Chế độ Batch: CHỈ lưu vào buffer, KHÔNG gửi realtime
      Serial.printf("[BATCH] Buffering: HR=%.0f, SpO2=%.0f, Steps=%u\n",
                    data.hr, data.spo2, steps);

      bool bufferFull = dataBuffer.addSample(data.hr, data.spo2, steps, alertScore,
                                             (uint8_t)activity, sleepDuration);
      if (bufferFull)
      {
        Serial.println("[BATCH] ⚠️ Buffer full - will send on next cycle");
      }
    }
  }
}

/**
 * @brief Cập nhật và gửi mức pin
 * TODO: Tạm thời dùng giá trị fake 75%
 */
void updateBattery()
{
  if (millis() - lastBatteryReadMs < BATTERY_UPDATE_INTERVAL_MS)
    return;
  lastBatteryReadMs = millis();

  uint8_t batteryPercent = powerManager.getBatteryPercent();

  bleManager.notifyBatteryLevel(batteryPercent);

  Serial.printf("[Main] Battery (FAKE): %d%%\n", batteryPercent);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32-C3 Health Monitor (Single Core) ===");

  // Khởi tạo Power Manager
  powerManager.begin();

  // Khởi tạo BLE
  bleManager.begin("Last Dance");

  // ESP32-C3: Tất cả dùng chung Wire
  Wire.begin(I2C_SDA_MAX30102, I2C_SCL_MAX30102);

  if (!mpuManager.begin(Wire, 0x68))
  {
    Serial.println("[MPU6050] Init failed");
  }

  // MAX30102 cũng dùng Wire (không phải Wire1)
  max30102Ready = max30102Manager.beginOnWire(Wire);
  if (!max30102Ready)
  {
    Serial.println("[Main] WARNING: MAX30102 not available - HR readings disabled");
  }

  // Reset buffer timer
  dataBuffer.resetSendTimer();

  Serial.println("[System] Running in BLE-only mode.");

  Serial.println("Setup complete.");
}

void loop()
{
  // 1. Đọc HR mỗi 1 giây và lưu vào buffer
  readAndBufferHR();

  // 2. Cập nhật step counter và sleep detection nếu được bật
  if (bleManager.isStepCountEnabled())
  {
    // Đảm bảo MPU6050 đang hoạt động (không ở sleep mode)
    if (mpuManager.isAsleep())
    {
      mpuManager.wake();
    }

    mpuManager.update();

    // Update sleep detection với heart rate data nếu có
    if (max30102Ready && max30102Manager.hasValidData())
    {
      Max30102Data data = max30102Manager.getCurrentData();
      mpuManager.updateSleepDetection((uint8_t)data.hr);
    }
  }
  else
  {
    // Đếm bước đã tắt - đặt MPU6050 vào sleep mode để tiết kiệm năng lượng
    if (!mpuManager.isAsleep())
    {
      mpuManager.sleep();
    }
  }

  // 2.5 Kiểm tra ngày mới để reset bước chân
  checkNewDay();

  // 3. Gửi batch data khi đủ điều kiện
  // Chỉ gửi nếu đang ở chế độ Batch
  if (bleManager.getDataTransmissionMode() == MODE_BATCH)
  {
    sendBatchData();
  }

  // 4. Cập nhật mức pin
  updateBattery();

  // Feed watchdog để tránh timeout
  yield();

  delay(10);
}