#include "wifi_manager.h"
#include <time.h>
#include <Arduino.h>

const char *WiFiManager::NTP_PRIMARY = "pool.ntp.org";
const char *WiFiManager::NTP_SECONDARY = "time.nist.gov";
const char *WiFiManager::NTP_TERTIARY = "time.google.com";
const long WiFiManager::NTP_GMT_OFFSET_SEC = 0;
const int WiFiManager::NTP_DAYLIGHT_OFFSET_SEC = 0;

WiFiManager::WiFiManager() : timeSynced(false)
{
}

void WiFiManager::connect(const char *ssid, const char *password)
{
    Serial.printf("[WiFi] Connecting to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    uint8_t spinner = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
        if (++spinner % 20 == 0)
        {
            Serial.println(" (still trying)");
        }
    }

    Serial.printf("\n[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    ensureTimeSynced();
    wifiClient.setHandshakeTimeout(30);
}

bool WiFiManager::isConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

void WiFiManager::ensureTimeSynced()
{
    if (timeSynced)
    {
        return;
    }

    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_PRIMARY, NTP_SECONDARY, NTP_TERTIARY);
    Serial.print("[Time] Waiting for NTP sync");

    const uint32_t timeoutMs = 15000;
    uint32_t start = millis();
    time_t now = 0;
    while ((millis() - start) < timeoutMs)
    {
        time(&now);
        if (now > 1609459200) // After Jan 1, 2021
        {
            timeSynced = true;
            Serial.println(" done.");
            return;
        }
        Serial.print('.');
        delay(500);
    }

    Serial.println(" failed (using unsynced clock).");
}

WiFiClientSecure &WiFiManager::getClient()
{
    return wifiClient;
}
