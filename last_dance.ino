/*
 * ESP32 Health Monitor - Multi-Core & Power-Saving
 * Cảm biến: MAX30102 (Heart Rate + SpO2)
 * Dựa trên code mẫu SparkFun MAX30105
 */

#include "config.h"
#include "sensor_manager.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "ml_model.h"
#include <Wire.h>
#include "HT_SSD1306Wire.h"

// RTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// --- Global objects ---
SensorManager sensorManager;
WiFiManager wifiManager;
MQTTClientManager mqttManager;
MLModel mlModel;

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

static void renderVitalsOnDisplay(float hr, float spo2)
{
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "ESP32 Health");

  char line1[24];
  char line2[24];
  snprintf(line1, sizeof(line1), "HR: %.0f bpm", hr);
  snprintf(line2, sizeof(line2), "SpO2: %.0f %%", spo2);
  display.drawString(0, 24, line1);
  display.drawString(0, 44, line2);
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
          profile.body_temperature,
          profile.age,
          profile.weight,
          profile.height,
          profile.gender);

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
  renderVitalsOnDisplay(0, 0);

  // Initialize sensor
  sensorManager.begin(I2C_SDA_MAX30102, I2C_SCL_MAX30102);

  // Connect WiFi
  wifiManager.connect(WIFI_SSID, WIFI_PASSWORD);

  // Setup MQTT
  mqttManager.begin(wifiManager.getClient(), MQTT_BROKER, MQTT_PORT,
                    MQTT_USERNAME, MQTT_PASSWORD);
  mqttManager.connect();

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
  // Maintain WiFi and MQTT connections
  if (!wifiManager.isConnected())
  {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 10000)
    {
      lastRetry = millis();
      wifiManager.connect(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  mqttManager.maintain();
  mqttManager.loop();

  // Read sensor data
  sensorManager.readSensorData();

  // Send valid data to ML task
  static unsigned long lastSendTime = 0;
  if (sensorManager.hasValidData() && (millis() - lastSendTime) > 500)
  {
    SensorData data = sensorManager.getCurrentData();
    // Try to enqueue for ML (non-blocking) but don't gate OLED/MQTT on this
    if (xQueueSend(xQueueSensorData, &data, 0) == pdTRUE)
    {
      Serial.println("[Main] Sensor data sent to ML queue");
    }
    else
    {
      Serial.println("[Main] ML queue full, skipping enqueue this cycle");
    }

    // Publish latest sensor data
    mqttManager.publishSensorData(data);

    // Update OLED with latest vitals regardless of queue state
    renderVitalsOnDisplay(data.hr, data.spo2);

    lastSendTime = millis();
  }

  // Handle alerts
  AlertData alertData;
  if (xQueueReceive(xQueueAlerts, &alertData, 0) == pdTRUE)
  {
    Serial.printf("[Main] ALERT: Abnormal vitals detected! Score=%.4f, HR=%.1f, SPO2=%.1f\n",
                  alertData.score, alertData.hr, alertData.spo2);
    mqttManager.publishAlert(alertData.score, alertData.hr, alertData.spo2);
  }

  delay(10); // Allow context switch
}