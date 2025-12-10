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

// === Battery update interval ===
#define BATTERY_UPDATE_INTERVAL_MS 60000 // Cập nhật pin mỗi 1 phút

struct AlertData
{
  float score;
  float hr;
  float spo2;
};

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
  float bmi = profile.weight / (profile.height * profile.height);
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

  // Chuẩn bị buffer để gửi (bao gồm steps hiện tại)
  uint32_t currentSteps = mpuManager.getStepCount();
  char jsonBuffer[4096];
  size_t len = dataBuffer.getCompressedData(jsonBuffer, sizeof(jsonBuffer), currentSteps);

  if (len > 0)
  {
    Serial.printf("[Main] JSON generated: %d bytes\n", len);
    if (bleManager.notifyHealthDataBatch(jsonBuffer, len))
    {
      Serial.println("[Main] ✅ Batch data sent successfully");
      dataBuffer.clear();
      Serial.println("[Main] ✅ Buffer cleared");
    }
    else
    {
      Serial.println("[Main] ❌ Failed to send batch data");
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
    processML(data.hr, data.spo2);

    // Gửi dữ liệu realtime qua BLE
    if (bleManager.isClientConnected())
    {
      uint32_t steps = mpuManager.getStepCount();
      bleManager.notifyHealthData(data.hr, data.spo2, steps);
    }

    if (bufferFull)
    {
      Serial.println("[Main] Buffer full - will send data");
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

  // TODO: Tạm thời comment - dùng giá trị fake
  // uint8_t batteryPercent = powerManager.getBatteryPercent();

  // Giá trị pin fake để test
  uint8_t batteryPercent = 75; // Fake battery level

  bleManager.notifyBatteryLevel(batteryPercent);

  Serial.printf("[Main] Battery (FAKE): %d%%\n", batteryPercent);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32-C3 Health Monitor (Single Core) ===");

  // Khởi tạo Power Manager
  // TODO: Tạm thời comment - dùng giá trị fake
  // powerManager.begin();

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

  // 3. Gửi batch data khi đủ điều kiện
  // Tạm thời disable batch data để gửi realtime
  // sendBatchData();

  // 4. Cập nhật mức pin
  updateBattery();

  // Feed watchdog để tránh timeout
  yield();

  // Cập nhật UI mỗi giây
  // static unsigned long lastUiUpdate = 0;
  // if (millis() - lastUiUpdate > 1000)
  // {
  //   Max30102Data d = max30102Manager.getCurrentData();
  //   uint32_t steps = mpuManager.getStepCount();

  //   unsigned long totalSeconds = millis() / 1000;
  //   int hour = (totalSeconds / 3600) % 24;
  //   int minute = (totalSeconds / 60) % 60;

  //   Serial.printf("[Status] HR=%.0f bpm, SpO2=%.0f%%, Steps=%d, Time=%02d:%02d\n", d.hr, d.spo2, steps, hour, minute);
  //   lastUiUpdate = millis();
  // }

  delay(10);
}