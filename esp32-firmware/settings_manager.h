#pragma once

#include <Arduino.h>

class SettingsManager {
public:
    void begin();

    // WiFi
    String getWiFiSSID();
    String getWiFiPassword();
    void setWiFi(const String& ssid, const String& password);
    bool hasWiFi();

    // Bridge server
    String getBridgeHost();
    uint16_t getBridgePort();
    void setBridge(const String& host, uint16_t port);
    bool hasBridge();

    bool isConfigured();
    void clear();

private:
    static constexpr const char* NVS_NAMESPACE = "ccdisplay";
    static constexpr const char* KEY_SSID      = "wifi_ssid";
    static constexpr const char* KEY_PASS      = "wifi_pass";
    static constexpr const char* KEY_BRIDGE_H  = "bridge_host";
    static constexpr const char* KEY_BRIDGE_P  = "bridge_port";
};
