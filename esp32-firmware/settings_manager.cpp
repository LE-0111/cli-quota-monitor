#include "settings_manager.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;

void SettingsManager::begin() {
    prefs.begin(NVS_NAMESPACE, false);
}

String SettingsManager::getWiFiSSID() {
    return prefs.getString(KEY_SSID, "");
}

String SettingsManager::getWiFiPassword() {
    return prefs.getString(KEY_PASS, "");
}

void SettingsManager::setWiFi(const String& ssid, const String& password) {
    prefs.putString(KEY_SSID, ssid);
    prefs.putString(KEY_PASS, password);
}

bool SettingsManager::hasWiFi() {
    return getWiFiSSID().length() > 0;
}

String SettingsManager::getBridgeHost() {
    return prefs.getString(KEY_BRIDGE_H, "");
}

uint16_t SettingsManager::getBridgePort() {
    return prefs.getUShort(KEY_BRIDGE_P, BRIDGE_DEFAULT_PORT);
}

void SettingsManager::setBridge(const String& host, uint16_t port) {
    prefs.putString(KEY_BRIDGE_H, host);
    prefs.putUShort(KEY_BRIDGE_P, port);
}

bool SettingsManager::hasBridge() {
    return getBridgeHost().length() > 0;
}

bool SettingsManager::isConfigured() {
    return hasWiFi() && hasBridge();
}

void SettingsManager::clear() {
    prefs.clear();
}
