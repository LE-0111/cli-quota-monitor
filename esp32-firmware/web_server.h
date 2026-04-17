#pragma once

#include <WebServer.h>
#include <DNSServer.h>
#include "settings_manager.h"

class ConfigWebServer {
public:
    void begin(SettingsManager* settings);
    void handleClient();
    void stop();
    bool isRunning() const { return _running; }

    void startAPMode();
    String getAPName() const { return _apName; }

    bool shouldReboot() const { return _shouldReboot; }

private:
    WebServer       _server;
    DNSServer       _dnsServer;
    SettingsManager* _settings = nullptr;
    bool            _running = false;
    bool            _shouldReboot = false;
    bool            _apMode = false;
    String          _apName;

    void handleRoot();
    void handleSave();
    void handleStatus();
    void handleReset();
    void handleNotFound();

    String buildPage();
    String buildSuccessPage();
};
