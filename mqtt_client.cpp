#include "mqtt_client.h"
#include "config.h"
#include <esp_system.h>
#include <Arduino.h>

const char MQTTClientManager::MQTT_ROOT_CA[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFBjCCAu6gAwIBAgIRAMISMktwqbSRcdxA9+KFJjwwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjQwMzEzMDAwMDAw
WhcNMjcwMzEyMjM1OTU5WjAzMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg
RW5jcnlwdDEMMAoGA1UEAxMDUjEyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB
CgKCAQEA2pgodK2+lP474B7i5Ut1qywSf+2nAzJ+Npfs6DGPpRONC5kuHs0BUT1M
5ShuCVUxqqUiXXL0LQfCTUA83wEjuXg39RplMjTmhnGdBO+ECFu9AhqZ66YBAJpz
kG2Pogeg0JfT2kVhgTU9FPnEwF9q3AuWGrCf4yrqvSrWmMebcas7dA8827JgvlpL
Thjp2ypzXIlhZZ7+7Tymy05v5J75AEaz/xlNKmOzjmbGGIVwx1Blbzt05UiDDwhY
XS0jnV6j/ujbAKHS9OMZTfLuevYnnuXNnC2i8n+cF63vEzc50bTILEHWhsDp7CH4
WRt/uTp8n1wBnWIEwii9Cq08yhDsGwIDAQABo4H4MIH1MA4GA1UdDwEB/wQEAwIB
hjAdBgNVHSUEFjAUBggrBgEFBQcDAgYIKwYBBQUHAwEwEgYDVR0TAQH/BAgwBgEB
/wIBADAdBgNVHQ4EFgQUALUp8i2ObzHom0yteD763OkM0dIwHwYDVR0jBBgwFoAU
ebRZ5nu25eQBc4AIiMgaWPbpm24wMgYIKwYBBQUHAQEEJjAkMCIGCCsGAQUFBzAC
hhZodHRwOi8veDEuaS5sZW5jci5vcmcvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMCcG
A1UdHwQgMB4wHKAaoBiGFmh0dHA6Ly94MS5jLmxlbmNyLm9yZy8wDQYJKoZIhvcN
AQELBQADggIBAI910AnPanZIZTKS3rVEyIV29BWEjAK/duuz8eL5boSoVpHhkkv3
4eoAeEiPdZLj5EZ7G2ArIK+gzhTlRQ1q4FKGpPPaFBSpqV/xbUb5UlAXQOnkHn3m
FVj+qYv87/WeY+Bm4sN3Ox8BhyaU7UAQ3LeZ7N1X01xxQe4wIAAE3JVLUCiHmZL+
qoCUtgYIFPgcg350QMUIWgxPXNGEncT921ne7nluI02V8pLUmClqXOsCwULw+PVO
ZCB7qOMxxMBoCUeL2Ll4oMpOSr5pJCpLN3tRA2s6P1KLs9TSrVhOk+7LX28NMUlI
usQ/nxLJID0RhAeFtPjyOCOscQBA53+NRjSCak7P4A5jX7ppmkcJECL+S0i3kXVU
y5Me5BbrU8973jZNv/ax6+ZK6TM8jWmimL6of6OrX7ZU6E2WqazzsFrLG3o2kySb
zlhSgJ81Cl4tv3SbYiYXnJExKQvzf83DYotox3f0fwv7xln1A2ZLplCb0O+l/AK0
YE0DS2FPxSAHi0iwMfW2nNHJrXcY3LLHD77gRgje4Eveubi2xxa+Nmk/hmhLdIET
iVDFanoCrMVIpQ59XWHkzdFmoHXHBV7oibVjGSO7ULSQ7MJ1Nz51phuDJSgAIU7A
0zrLnOrAj/dfrlEWRhCvAgbuwLZX1A2sjNjXoPOHbsPiy+lO1KF8/XY7
-----END CERTIFICATE-----
)EOF";

MQTTClientManager::MQTTClientManager()
    : mqttClient(), lastReconnectAttempt(0), wifiClientPtr(nullptr)
{
    topicData = MQTT_TOPIC_DATA;
    topicAlert = MQTT_TOPIC_ALERT;
}

void MQTTClientManager::begin(WiFiClientSecure &wifiClient, const char *broker, uint16_t port,
                              const char *username, const char *password)
{
    this->wifiClientPtr = &wifiClient;
    this->broker = broker;
    this->port = port;
    this->username = username;
    this->password = password;

    wifiClient.setCACert(MQTT_ROOT_CA);
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(broker, port);
    mqttClient.setKeepAlive(30);
    mqttClient.setBufferSize(256);
}

void MQTTClientManager::setCACert(const char *rootCA)
{
    if (wifiClientPtr != nullptr)
    {
        wifiClientPtr->setCACert(rootCA);
    }
}

bool MQTTClientManager::connect()
{
    if (mqttClient.connected())
    {
        return true;
    }

    Serial.printf("[MQTT] Connecting to %s:%u\n", broker, port);

    String clientId = "ESP32Health-";
    clientId += String((uint64_t)ESP.getEfuseMac(), HEX);

    bool connected = mqttClient.connect(clientId.c_str(), username, password);
    if (connected)
    {
        Serial.println("[MQTT] Connected.");
    }
    else
    {
        Serial.printf("[MQTT] Connect failed, state=%d\n", mqttClient.state());
        if (wifiClientPtr != nullptr)
        {
            char errorBuffer[120];
            if (wifiClientPtr->lastError(errorBuffer, sizeof(errorBuffer)) != 0)
            {
                Serial.printf("[MQTT] TLS error: %s\n", errorBuffer);
            }
        }
    }

    return connected;
}

void MQTTClientManager::maintain()
{
    if (!mqttClient.connected())
    {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 5000)
        {
            lastReconnectAttempt = now;
            connect();
        }
    }
}

void MQTTClientManager::loop()
{
    mqttClient.loop();
}

bool MQTTClientManager::isConnected()
{
    return mqttClient.connected();
}

void MQTTClientManager::publishSensorData(const SensorData &data)
{
    if (!mqttClient.connected())
    {
        connect();
        if (!mqttClient.connected())
        {
            return;
        }
    }

    char payload[96];
    int written = snprintf(payload, sizeof(payload), "{\"heart_rate\":%.1f,\"spo2\":%.1f}", data.hr, data.spo2);
    if (written < 0 || written >= (int)sizeof(payload))
    {
        Serial.println("[MQTT] Payload formatting failed.");
        return;
    }

    if (!mqttClient.publish(topicData, payload, false))
    {
        Serial.println("[MQTT] Failed to publish sensor data.");
    }
    else
    {
        Serial.println("[MQTT] Sensor data published.");
    }
}

void MQTTClientManager::publishAlert(float score, float hr, float spo2)
{
    if (!mqttClient.connected())
    {
        connect();
        if (!mqttClient.connected())
        {
            return;
        }
    }

    char payload[128];
    int written = snprintf(payload, sizeof(payload),
                           "{\"alert_score\":%.4f,\"heart_rate\":%.1f,\"spo2\":%.1f}",
                           score, hr, spo2);
    if (written < 0 || written >= (int)sizeof(payload))
    {
        Serial.println("[MQTT] Alert payload formatting failed.");
        return;
    }

    if (!mqttClient.publish(topicAlert, payload, false))
    {
        Serial.println("[MQTT] Failed to publish alert.");
    }
    else
    {
        Serial.println("[MQTT] Alert published.");
    }
}
