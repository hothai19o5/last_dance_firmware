#pragma once
#include <WiFi.h>
#include <WiFiClientSecure.h>

class WiFiManager
{
public:
    WiFiManager();
    void connect(const char *ssid, const char *password);
    bool isConnected();
    void ensureTimeSynced();
    WiFiClientSecure &getClient();

private:
    WiFiClientSecure wifiClient;
    bool timeSynced;

    static const char *NTP_PRIMARY;
    static const char *NTP_SECONDARY;
    static const char *NTP_TERTIARY;
    static const long NTP_GMT_OFFSET_SEC;
    static const int NTP_DAYLIGHT_OFFSET_SEC;
};
