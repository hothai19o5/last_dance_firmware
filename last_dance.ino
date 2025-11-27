/**
 * @file last_dance.ino
 * @brief Chương trình chính cho thiết bị theo doi suc khoe
 * @author Ho Xuan Thai
 * @date 2025
 *
 * Tổng quan:
 * - Thiết bị đo nhịp tim, SpO2, đếm bước, tính calo
 * - Sử dụng máy học để phát hiện bất thường sức khỏe
 * - Giao tiếp với ứng dụng di động qua BLE
 * - Chạy trên ESP32 với RTOS (FreeRTOS)
 *
 * Kiến trúc:
 * - Core 0: ML Task - chạy suy diễn và phát hiện bất thường (ưu tiên cao)
 * - Core 1: Main Loop - đọc cảm biến và cập nhật UI
 *
 * Cảm biến:
 * - MAX30102 (Wire1): Nhịp tim, SpO2
 * - MPU6050 (Wire): Gia tốc, đếm bước
 * - OLED (Heltec): Hiển thị thông tin
 *
 * BLE Services:
 * - User Profile Service: Nhận cài đặt từ ứng dụng
 * - Health Data Service: Gửi dữ liệu sức khỏe
 */

#include "max30102_manager.h"
#include "ml_model.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"
#include "mpu6050_manager.h"
#include "ble_service_manager.h"
#include "calorie_manager.h"
#include <time.h>

// RTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// === Global Objects ===
SensorManager sensorManager;   ///< Quản lý cảm biến MAX30102
MLModel mlModel;               ///< Mô hình máy học
MPU6050Manager mpuManager;     ///< Quản lý đếm bước
BLEServiceManager bleManager;  ///< Quản lý BLE
CalorieManager calorieManager; ///< Quản lý tính calo

// === Hàng đợi RTOS ===
QueueHandle_t xQueueSensorData; ///< Hàng đợi truyền dữ liệu cảm biến từ main đến ML task
QueueHandle_t xQueueAlerts;     ///< Hàng đợi truyền cảnh báo từ ML task về main

/**
 * @struct AlertData
 * @brief Cấu trúc lưu trữ dữ liệu cảnh báo bất thường
 */
struct AlertData
{
  float score; ///< Điểm cảnh báo (0-1) từ mô hình ML
  float hr;    ///< Nhịp tim tại thời điểm cảnh báo
  float spo2;  ///< SpO2 tại thời điểm cảnh báo
};

// === OLED (Heltec WiFi Kit 32) ===
#define SDA_OLED 4  ///< Chân SDA của OLED trên Heltec
#define SCL_OLED 15 ///< Chân SCL của OLED trên Heltec
#define RST_OLED 16 ///< Chân RESET của OLED trên Heltec
#define Vext 21     ///< Chân điều khiển năng lượng OLED trên Heltec

static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

/**
 * @brief Bật nguồn cho OLED (Heltec specific)
 */
static inline void VextON()
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // LOW = bật Vext trên Heltec
}

/**
 * @brief Khởi tạo màn hình OLED
 *
 * Quá trình:
 * 1. Bật nguồn OLED
 * 2. Khởi tạo Wire (I2C) với chân được chỉ định
 * 3. Khởi tạo OLED driver
 * 4. Lật màn hình dọc và cài đặt căn lề trái
 */
static void initDisplay()
{
  VextON();
  delay(100);
  // Khởi tạo bus I2C cho OLED trên chân Heltec
  Wire.begin(SDA_OLED, SCL_OLED);
  display.init();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * @brief Cập nhật hiển thị thông tin sức khỏe trên OLED
 *
 * Hiển thị:
 * - Dòng 1: Nhịp tim (BPM) | Pin (%)
 * - Dòng 2: SpO2 (%)
 * - Dòng 3: Calo (kcal)
 * - Dòng 4: Bước | Giờ:phút
 *
 * @param hr Nhịp tim
 * @param spo2 SpO2
 * @param calories Calo tiêu thụ
 * @param steps Số bước
 * @param batteryLevel Pin (%)
 * @param hour Giờ
 * @param minute Phút
 */
static void renderDisplay(float hr, float spo2, float calories, int steps, int batteryLevel, int hour, int minute)
{
  display.clear();
  display.setFont(ArialMT_Plain_10);

  char line1[24], line2[24], line3[24], line4[24], line5[24], line6[24];

  snprintf(line1, sizeof(line1), "%.0f bpm", hr);
  snprintf(line2, sizeof(line2), "%.0f %% O2", spo2);
  snprintf(line3, sizeof(line3), "%.1f kcal", calories);
  snprintf(line4, sizeof(line4), "%d steps", steps);
  snprintf(line5, sizeof(line5), "Bat: %d %%", batteryLevel);

  display.drawString(5, 5, line1);
  display.drawString(80, 5, line5);
  display.drawString(5, 17, line2);
  display.drawString(5, 29, line3);
  display.drawString(5, 41, line4);

  display.setFont(ArialMT_Plain_24);
  snprintf(line6, sizeof(line6), "%02d:%02d", hour, minute);
  display.drawString(64, 29, line6);

  display.display();
}

/**
 * @brief ML Task - Chạy trên Core 0 (ưu tiên cao)
 *
 * Chức năng:
 * 1. Nhận dữ liệu cảm biến từ hàng đợi xQueueSensorData
 * 2. Chạy suy diễn mô hình ML
 * 3. Nếu điểm cảnh báo > 0.95, gửi cảnh báo vào xQueueAlerts
 * 4. Lặp lại vô hạn
 *
 * @param pvParameters Tham số được truyền khi tạo task (không dùng)
 */
void mlTask(void *pvParameters)
{
  Serial.println("ML Task started (Core 0)");
  mlModel.setup();

  SensorData data;
  for (;;)
  {
    // Chờ nhận dữ liệu từ main task
    if (xQueueReceive(xQueueSensorData, &data, portMAX_DELAY) == pdTRUE)
    {
      Serial.printf("[ML] Got data: HR=%.1f, SPO2=%.1f\n", data.hr, data.spo2);

      // Lấy hồ sơ người dùng từ BLE
      UserProfile &profile = sensorManager.getUserProfile();

      // Tính BMI: cân nặng / (chiều cao^2)
      float score = mlModel.runInference(
          data.hr,
          data.spo2,
          profile.weight / (profile.height * profile.height));

      // Nếu điểm cảnh báo cao (bất thường), gửi vào hàng đợi
      if (score > 0.95)
      {
        AlertData alert;
        alert.score = score;
        alert.hr = data.hr;
        alert.spo2 = data.spo2;
        xQueueSend(xQueueAlerts, &alert, 0);
      }
    }
  }
}

/**
 * @brief Setup - Khởi tạo toàn bộ hệ thống
 *
 * Thực hiện các bước:
 * 1. Khởi tạo Serial (debug)
 * 2. Khởi tạo màn hình OLED
 * 3. Khởi tạo BLE
 * 4. Khởi tạo cảm biến (MAX30102, MPU6050)
 * 5. Tạo các hàng đợi RTOS
 * 6. Tạo ML Task trên Core 0
 */
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32 Health Monitor ===");

  // Khởi tạo màn hình OLED
  initDisplay();
  renderDisplay(0, 0, 0, 0, 0, 0, 0);

  // Khởi tạo BLE (ưu tiên cao vì là gateway chính với ứng dụng di động)
  bleManager.begin("Last Dance");

  // Khởi tạo MPU6050 trên bus I2C chính (đếm bước)
  if (!mpuManager.begin(Wire, 0x68))
  {
    Serial.println("[MPU6050] Init failed (check wiring). Steps will remain 0.");
  }

  // Khởi tạo cảm biến MAX30102 trên Wire1 (nhịp tim + SpO2)
  sensorManager.begin(I2C_SDA_MAX30102, I2C_SCL_MAX30102);

  Serial.println("[System] Running in BLE-only mode. Mobile app is the gateway.");

  // Tạo các hàng đợi RTOS
  xQueueSensorData = xQueueCreate(5, sizeof(SensorData)); // Hàng đợi 5 phần tử
  xQueueAlerts = xQueueCreate(5, sizeof(AlertData));

  if (xQueueSensorData == NULL || xQueueAlerts == NULL)
  {
    Serial.println("Queue creation failed!");
    while (1)
      ; // Dừng nếu tạo hàng đợi thất bại
  }

  // Tạo ML Task trên Core 0 với ưu tiên cao
  xTaskCreatePinnedToCore(
      mlTask,   // Hàm task
      "MLTask", // Tên task
      10240,    // Kích thước stack (byte)
      NULL,     // Tham số
      1,        // Ưu tiên (0=thấp, 3=cao)
      NULL,     // Handle task
      0);       // Core 0

  Serial.println("Setup complete. Reading sensor...");
}

/**
 * @brief Main Loop - Chạy trên Core 1 (ưu tiên thấp)
 *
 * Chức năng chính:
 * 1. Đọc dữ liệu từ cảm biến mỗi 5 giây
 * 2. Cập nhật bộ đếm bước từ MPU6050
 * 3. Mỗi 500ms: gửi dữ liệu hợp lệ đến ML task
 * 4. Mỗi 1 giây: cập nhật UI (OLED)
 * 5. Mỗi 5 phút: gửi dữ liệu qua BLE
 * 6. Xử lý các cảnh báo nhận được từ ML task (gửi ngay lập tức)
 */
void loop()
{
  // === Đọc dữ liệu cảm biến (mỗi 5 giây) ===
  // static unsigned long lastSensorReadTime = 0;
  // if (millis() - lastSensorReadTime >= 1000)
  // {
  //   sensorManager.readSensorData();
  //   lastSensorReadTime = millis();
  // }

  sensorManager.readSensorData();

  // === Cập nhật bộ đếm bước từ MPU6050 (nếu được bật) ===
  if (bleManager.isStepCountEnabled())
  {
    mpuManager.update();
  }

  // === Cập nhật calo tiêu thụ ===
  if (sensorManager.hasValidData())
  {
    SensorData data = sensorManager.getCurrentData();
    UserProfile &profile = bleManager.getUserProfile(); // Lấy hồ sơ từ BLE
    calorieManager.update(mpuManager.getStepCount(), data.hr, profile);
  }

  // === Gửi dữ liệu đến ML Task (mỗi 500ms) ===
  static unsigned long lastSendTime = 0;
  if (sensorManager.hasValidData() && (millis() - lastSendTime) > 500)
  {
    SensorData data = sensorManager.getCurrentData();

    // Thử đưa vào hàng đợi (non-blocking)
    if (xQueueSend(xQueueSensorData, &data, 0) == pdTRUE)
    {
      Serial.println("[Main] Sensor data sent to ML queue");
    }
    else
    {
      Serial.println("[Main] ML queue full, skipping enqueue this cycle");
    }

    lastSendTime = millis();

    // === Cập nhật UI (mỗi 1 giây) ===
    static unsigned long lastUiUpdate = 0;
    if (millis() - lastUiUpdate > 1000)
    {
      SensorData d = sensorManager.getCurrentData();
      uint32_t steps = mpuManager.getStepCount();
      float calories = calorieManager.getTotalCalories();

      // Tính thời gian từ lúc khởi động (sử dụng millis)
      // Vì chuyên độ BLE-only, không có NTP time server
      unsigned long totalSeconds = millis() / 1000;
      int hour = (totalSeconds / 3600) % 24;
      int minute = (totalSeconds / 60) % 60;

      // Cập nhật OLED
      renderDisplay(d.hr, d.spo2, calories, steps, 85, hour, minute);

      lastUiUpdate = millis();
    }

    // === Gửi dữ liệu BLE (mỗi 5 phút) ===
    static unsigned long lastBleNotifyTime = 0;
    if (millis() - lastBleNotifyTime >= 300000) // 5 phút = 300000ms
    {
      SensorData d = sensorManager.getCurrentData();
      uint32_t steps = mpuManager.getStepCount();
      float calories = calorieManager.getTotalCalories();

      // Gửi dữ liệu qua BLE nếu có kết nối
      if (bleManager.isClientConnected())
      {
        bleManager.notifyHealthData(d.hr, d.spo2, steps, calories);
        Serial.println("[Main] BLE data sent (5-minute interval)");
      }

      lastBleNotifyTime = millis();
    }

    lastSendTime = millis();
  }

  // === Xử lý các cảnh báo từ ML task ===
  AlertData alertData;
  if (xQueueReceive(xQueueAlerts, &alertData, 0) == pdTRUE)
  {
    Serial.printf("[Main] ALERT: Abnormal vitals detected! Score=%.4f, HR=%.1f, SPO2=%.1f\n",
                  alertData.score, alertData.hr, alertData.spo2);

    // Trong chế độ BLE-only, gửi cảnh báo cùng điểm số đến ứng dụng di động
    if (bleManager.isClientConnected())
    {
      uint32_t steps = mpuManager.getStepCount();
      float calories = calorieManager.getTotalCalories();
      bleManager.notifyHealthDataWithAlert(alertData.hr, alertData.spo2, steps, calories, alertData.score);
    }
  }

  delay(10); // Cho phép context switch (tránh watchdog timeout)
}