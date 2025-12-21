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
 * @brief Kiểm tra xem đã qua ngày mới chưa để reset số bước
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
    Serial.printf("[System] New day detected: %d -> %d. Resetting steps.\n",
                  lastDayProcessed, timeinfo->tm_mday);
    mpuManager.resetStepCount();
    lastDayProcessed = timeinfo->tm_mday;
  }
}

/**
 * @brief Xử lý ML với dữ liệu HR/SpO2 mới nhất từ buffer
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

  if (score > 1)
  {
    Serial.printf("[ML] ALERT: Score=%.4f\n", score);

    if (bleManager.isClientConnected())
    {
      uint32_t steps = mpuManager.getStepCount();
      bleManager.notifyHealthDataWithAlert(hr, spo2, steps, score);
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

  Serial.println("[Main] ========== SENDING BATCH DATA ==========");
  Serial.printf("[Main] Buffer has %d samples ready to send\n", dataBuffer.getCount());

  // Chuẩn bị buffer để gửi
  uint8_t binaryBuffer[4096];
  size_t len = dataBuffer.getBinaryData(binaryBuffer, sizeof(binaryBuffer));

  if (len > 0)
  {
    Serial.printf("[Main] Binary data generated: %d bytes\n", len);
    if (bleManager.notifyHealthDataBatch(binaryBuffer, len))
    {
      Serial.println("[Main] Batch data sent successfully");
      dataBuffer.clear();
      Serial.println("[Main] Buffer cleared");
    }
    else
    {
      Serial.println("[Main] Failed to send batch data");
    }
  }

  Serial.println("[Main] ==========================================");

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

    // Thêm vào buffer
    // bool bufferFull = dataBuffer.addSample(data.hr, data.spo2);
    // Tạm thời disable buffer để gửi realtime
    bool bufferFull = false;

    // Chạy ML với dữ liệu mới nhất (đồng bộ với việc đọc HR)
    // Chỉ chạy nếu được enable qua BLE
    if (bleManager.isMLEnabled())
    {
      processML(data.hr, data.spo2);
    }

    // Xử lý gửi dữ liệu dựa trên chế độ
    DataTransmissionMode mode = bleManager.getDataTransmissionMode();

    if (mode == MODE_REALTIME)
    {
      // Chế độ Realtime: Gửi ngay lập tức, KHÔNG lưu buffer
      if (bleManager.isClientConnected())
      {
        uint32_t steps = mpuManager.getStepCount();
        bleManager.notifyHealthData(data.hr, data.spo2, steps);
      }
    }
    else // MODE_BATCH
    {
      // Chế độ Batch: Lưu vào buffer, KHÔNG gửi ngay
      uint32_t currentSteps = mpuManager.getStepCount();
      bool bufferFull = dataBuffer.addSample(data.hr, data.spo2, currentSteps);
      if (bufferFull)
      {
        Serial.println("[Main] Buffer full - ready to send batch");
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

  // 2. Cập nhật step counter nếu được bật
  if (bleManager.isStepCountEnabled())
  {
    mpuManager.update();
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