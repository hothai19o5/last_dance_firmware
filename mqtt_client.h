#pragma once
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include "sensor_manager.h"

class MQTTClientManager
{
public:
    MQTTClientManager();
    void begin(WiFiClientSecure &wifiClient, const char *broker, uint16_t port,
               const char *username, const char *password);
    void setCACert(const char *rootCA);
    bool connect();
    void maintain();
    void loop();
    bool isConnected();
    void publishSensorData(const SensorData &data);
    void publishAlert(float score);

private:
    PubSubClient mqttClient;
    const char *broker;
    uint16_t port;
    const char *username;
    const char *password;
    const char *topicData;
    const char *topicAlert;
    WiFiClientSecure *wifiClientPtr;
    unsigned long lastReconnectAttempt;

    static const char MQTT_ROOT_CA[];
};
