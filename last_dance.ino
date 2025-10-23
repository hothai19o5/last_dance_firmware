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

  // Initialize sensor
  sensorManager.begin(I2C_SDA, I2C_SCL);

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

    if (xQueueSend(xQueueSensorData, &data, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      Serial.println("[Main] Sensor data sent to ML queue");
      mqttManager.publishSensorData(data);
      lastSendTime = millis();
    }
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