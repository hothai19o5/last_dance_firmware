#include "sensor_manager.h"
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

// --- Global objects ---
SensorManager sensorManager;
MLModel mlModel;
MPU6050Manager mpuManager;     // Step counter (MPU6050)
BLEServiceManager bleManager;  // BLE Gateway for Mobile App
CalorieManager calorieManager; // Calorie tracking

// --- RTOS Queues ---
QueueHandle_t xQueueSensorData;
QueueHandle_t xQueueAlerts;

// Alert data structure
struct AlertData
{
  float score;
  float hr;
  float spo2;
};

// --- OLED (Heltec WiFi Kit 32) ---
#define SDA_OLED 4
#define SCL_OLED 15
#define RST_OLED 16
#define Vext 21 // Control external power for OLED on Heltec boards
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

static inline void VextON()
{
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // LOW to enable Vext on Heltec
}

static void initDisplay()
{
  VextON();
  delay(100);
  // Initialize primary I2C bus for OLED on Heltec pins
  Wire.begin(SDA_OLED, SCL_OLED);
  display.init();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

//
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

// --- ML Task (Core 0) ---
void mlTask(void *pvParameters)
{
  Serial.println("ML Task started (Core 0)");
  mlModel.setup();

  SensorData data;
  for (;;)
  {
    if (xQueueReceive(xQueueSensorData, &data, portMAX_DELAY) == pdTRUE)
    {
      Serial.printf("[ML] Got data: HR=%.1f, SPO2=%.1f\n", data.hr, data.spo2);

      UserProfile &profile = sensorManager.getUserProfile();
      float score = mlModel.runInference(
          data.hr,
          data.spo2,
          profile.weight / (profile.height * profile.height) // BMI
      );

      if (score > 0.75)
      {
        AlertData alertData;
        alertData.score = score;
        alertData.hr = data.hr;
        alertData.spo2 = data.spo2;
        xQueueSend(xQueueAlerts, &alertData, pdMS_TO_TICKS(10));
      }
    }
  }
}

// --- Setup ---
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32 Health Monitor ===");

  // Init OLED display
  initDisplay();
  renderDisplay(0, 0, 0, 0, 0, 0, 0);

  // Init BLE Service (highest priority for Mobile App gateway)
  bleManager.begin("ESP32-Health-Watch");

  // Init MPU6050 on same I2C bus as OLED (Wire)
  if (!mpuManager.begin(Wire, 0x68))
  {
    Serial.println("[MPU6050] Init failed (check wiring). Steps will remain 0.");
  }

  // Initialize sensor
  sensorManager.begin(I2C_SDA_MAX30102, I2C_SCL_MAX30102);

  // WiFi and MQTT disabled in BLE-only mode
  // wifiManager.connect(WIFI_SSID, WIFI_PASSWORD);
  // mqttManager.begin(wifiManager.getClient(), MQTT_BROKER, MQTT_PORT,
  //                   MQTT_USERNAME, MQTT_PASSWORD);
  // mqttManager.connect();

  Serial.println("[System] Running in BLE-only mode. Mobile app is the gateway.");

  // Create RTOS queues
  xQueueSensorData = xQueueCreate(5, sizeof(SensorData));
  xQueueAlerts = xQueueCreate(5, sizeof(AlertData));

  if (xQueueSensorData == NULL || xQueueAlerts == NULL)
  {
    Serial.println("Queue creation failed!");
    while (1)
      ;
  }

  // Create ML Task on Core 0
  xTaskCreatePinnedToCore(
      mlTask,
      "MLTask",
      10240,
      NULL,
      1,
      NULL,
      0);

  Serial.println("Setup complete. Reading sensor...");
}

// --- Main loop (Core 1) ---
void loop()
{
  // WiFi/MQTT disabled in BLE-only mode
  // All data sync happens via BLE notifications to mobile app

  // Read sensor data
  sensorManager.readSensorData();
  // Update step counter from MPU6050
  mpuManager.update();

  // Update calorie tracker with current steps and HR
  if (sensorManager.hasValidData())
  {
    SensorData data = sensorManager.getCurrentData();
    UserProfile &profile = bleManager.getUserProfile(); // Use BLE-synced profile
    calorieManager.update(mpuManager.getStepCount(), data.hr, profile);
  };

  // Send valid data to ML task
  static unsigned long lastSendTime = 0;
  if (sensorManager.hasValidData() && (millis() - lastSendTime) > 500)
  {
    SensorData data = sensorManager.getCurrentData();
    // Try to enqueue for ML (non-blocking)
    if (xQueueSend(xQueueSensorData, &data, 0) == pdTRUE)
    {
      Serial.println("[Main] Sensor data sent to ML queue");
    }
    else
    {
      Serial.println("[Main] ML queue full, skipping enqueue this cycle");
    }

    // Update rate limit timestamp for ML/data pipeline
    lastSendTime = millis();

    // Periodic UI update (once per second) with real steps and current time
    static unsigned long lastUiUpdate = 0;
    if (millis() - lastUiUpdate > 1000)
    {
      SensorData d = sensorManager.getCurrentData();
      uint32_t steps = mpuManager.getStepCount();
      float calories = calorieManager.getTotalCalories();

      // Use millis for time display in BLE-only mode (no NTP)
      unsigned long totalSeconds = millis() / 1000;
      int hour = (totalSeconds / 3600) % 24;
      int minute = (totalSeconds / 60) % 60;

      // Display real calories and steps
      renderDisplay(d.hr, d.spo2, calories, (int)steps, 85, hour, minute);

      // Notify BLE client if connected
      if (bleManager.isClientConnected())
      {
        bleManager.notifyHealthData(d.hr, d.spo2, steps, calories);
      }

      lastUiUpdate = millis();
    }

    lastSendTime = millis();
  }

  // Handle alerts - send via BLE instead of MQTT
  AlertData alertData;
  if (xQueueReceive(xQueueAlerts, &alertData, 0) == pdTRUE)
  {
    Serial.printf("[Main] ALERT: Abnormal vitals detected! Score=%.4f, HR=%.1f, SPO2=%.1f\n",
                  alertData.score, alertData.hr, alertData.spo2);
    // In BLE-only mode, send alert with score to mobile app
    if (bleManager.isClientConnected())
    {
      bleManager.notifyHealthDataWithAlert(alertData.hr, alertData.spo2,
                                           mpuManager.getStepCount(),
                                           calorieManager.getTotalCalories(),
                                           alertData.score);
    }
  }

  delay(10); // Allow context switch
}