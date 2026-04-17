#include "web_server.h"
#include "config.h"
#include <WiFi.h>

static const char HTML_HEADER[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>CC Display Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;
background:#0d1117;color:#e6edf3;min-height:100vh;display:flex;
justify-content:center;align-items:center;padding:20px}
.container{max-width:460px;width:100%;background:#161b22;
border-radius:12px;padding:32px;border:1px solid #30363d}
h1{color:#ff9944;font-size:22px;text-align:center;margin-bottom:4px}
.subtitle{color:#8b949e;text-align:center;font-size:13px;margin-bottom:24px}
label{display:block;color:#8b949e;font-size:13px;margin-bottom:6px}
input[type="text"],input[type="password"],input[type="number"]{
width:100%;padding:10px 12px;background:#0d1117;border:1px solid #30363d;
border-radius:6px;color:#e6edf3;font-size:14px;margin-bottom:12px;outline:none}
input:focus{border-color:#ff9944}
input::placeholder{color:#484f58}
.btn{width:100%;padding:11px;background:#ff9944;color:#0d1117;
border:none;border-radius:6px;font-size:15px;font-weight:600;
cursor:pointer;margin-top:8px}
.btn:hover{background:#ffaa66}
.btn-danger{background:transparent;border:1px solid #f85149;
color:#f85149;margin-top:20px;font-size:13px;padding:8px}
.btn-danger:hover{background:#f8514920}
.hint{color:#484f58;font-size:12px;margin-top:-8px;margin-bottom:12px}
.section{border-top:1px solid #21262d;margin-top:20px;padding-top:20px}
.section h2{color:#e6edf3;font-size:16px;margin-bottom:16px}
.status{color:#00ff88;font-size:13px;margin-bottom:12px}
.footer{text-align:center;color:#484f58;font-size:11px;margin-top:24px}
</style>
</head>
<body>
<div class="container">
)rawliteral";

static const char HTML_FOOTER[] PROGMEM = R"rawliteral(
<div class="footer">CC Display v1.0</div>
</div>
</body>
</html>
)rawliteral";

void ConfigWebServer::startAPMode() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%02X%02X", mac[4], mac[5]);
    _apName = String("CCDisplay-") + suffix;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(_apName.c_str());

    _dnsServer.start(53, "*", WiFi.softAPIP());
    _apMode = true;

    Serial.printf("AP started: %s @ %s\n", _apName.c_str(),
                  WiFi.softAPIP().toString().c_str());
}

void ConfigWebServer::begin(SettingsManager* settings) {
    _settings = settings;

    _server.on("/", HTTP_GET,  [this]() { handleRoot(); });
    _server.on("/save", HTTP_POST, [this]() { handleSave(); });
    _server.on("/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/reset", HTTP_POST, [this]() { handleReset(); });
    _server.onNotFound([this]() { handleNotFound(); });

    _server.begin(80);
    _running = true;
    Serial.println("Web server started on port 80");
}

void ConfigWebServer::handleClient() {
    if (_running) {
        if (_apMode) {
            _dnsServer.processNextRequest();
        }
        _server.handleClient();
    }
}

void ConfigWebServer::stop() {
    if (_running) {
        _server.stop();
        _running = false;
    }
}

void ConfigWebServer::handleRoot() {
    _server.send(200, "text/html", buildPage());
}

void ConfigWebServer::handleSave() {
    if (!_settings) {
        _server.send(500, "text/plain", "Internal error");
        return;
    }

    String ssid = _server.arg("ssid");
    String pass = _server.arg("password");
    String host = _server.arg("bridge_host");
    String portStr = _server.arg("bridge_port");

    if (ssid.length() == 0) {
        _server.send(400, "text/plain", "WiFi SSID is required");
        return;
    }
    if (host.length() == 0) {
        _server.send(400, "text/plain", "Bridge server IP is required");
        return;
    }

    uint16_t port = BRIDGE_DEFAULT_PORT;
    if (portStr.length() > 0) {
        port = portStr.toInt();
    }

    _settings->setWiFi(ssid, pass);
    _settings->setBridge(host, port);

    Serial.printf("Config saved: WiFi=%s, Bridge=%s:%d\n",
                  ssid.c_str(), host.c_str(), port);

    _server.send(200, "text/html", buildSuccessPage());
    _shouldReboot = true;
}

void ConfigWebServer::handleStatus() {
    String json = "{";
    json += "\"configured\":" + String(_settings->isConfigured() ? "true" : "false") + ",";
    json += "\"wifi_ssid\":\"" + _settings->getWiFiSSID() + "\",";
    json += "\"bridge_host\":\"" + _settings->getBridgeHost() + "\",";
    json += "\"bridge_port\":" + String(_settings->getBridgePort()) + ",";
    json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"heap\":" + String(ESP.getFreeHeap());
    json += "}";
    _server.send(200, "application/json", json);
}

void ConfigWebServer::handleReset() {
    if (_settings) {
        _settings->clear();
        Serial.println("Settings cleared!");
    }
    _server.send(200, "text/html", buildSuccessPage());
    _shouldReboot = true;
}

void ConfigWebServer::handleNotFound() {
    _server.sendHeader("Location", "/");
    _server.send(302, "text/plain", "");
}

String ConfigWebServer::buildPage() {
    String page;
    page.reserve(4096);

    String ssid = _settings ? _settings->getWiFiSSID() : "";
    String bridgeHost = _settings ? _settings->getBridgeHost() : "";
    uint16_t bridgePort = _settings ? _settings->getBridgePort() : BRIDGE_DEFAULT_PORT;
    bool wifiOk = WiFi.status() == WL_CONNECTED;

    page += FPSTR(HTML_HEADER);

    page += F("<h1>CC Display</h1>");
    page += F("<p class='subtitle'>Claude Code &amp; Codex Usage Monitor</p>");

    if (wifiOk) {
        page += "<div class='status'>Connected to <b>" + ssid + "</b> &mdash; " +
                WiFi.localIP().toString() + "</div>";
    }

    page += F("<form method='POST' action='/save'>");

    // WiFi section
    page += F("<label for='ssid'>WiFi Network (SSID)</label>");
    page += "<input type='text' id='ssid' name='ssid' placeholder='Your WiFi name' value='" + ssid + "'>";
    page += F("<label for='password'>WiFi Password</label>");
    page += F("<input type='password' id='password' name='password' placeholder='WiFi password'>");

    // Bridge section
    page += F("<div class='section'><h2>Bridge Server</h2></div>");
    page += F("<label for='bridge_host'>Mac IP Address</label>");
    page += "<input type='text' id='bridge_host' name='bridge_host' placeholder='192.168.1.xxx' value='" + bridgeHost + "'>";
    page += F("<div class='hint'>Run bridge_server.py on your Mac, then enter its local IP here.</div>");

    page += F("<label for='bridge_port'>Port</label>");
    page += "<input type='number' id='bridge_port' name='bridge_port' value='" + String(bridgePort) + "'>";

    page += F("<button type='submit' class='btn'>Save &amp; Restart</button>");
    page += F("</form>");

    // Reset
    page += F("<form method='POST' action='/reset'>");
    page += F("<button type='submit' class='btn btn-danger' ");
    page += F("onclick='return confirm(\"Clear all settings?\")'>Reset All</button>");
    page += F("</form>");

    page += FPSTR(HTML_FOOTER);
    return page;
}

String ConfigWebServer::buildSuccessPage() {
    String page;
    page.reserve(1024);

    page += FPSTR(HTML_HEADER);
    page += F("<h1>Settings Saved</h1>");
    page += F("<p class='subtitle'>Restarting device...</p>");
    page += F("<div style='text-align:center;margin:40px 0'>");
    page += F("<div style='color:#00ff88;font-size:48px'>&#10003;</div>");
    page += F("<p style='color:#8b949e;margin-top:16px'>Reconnect to your WiFi network to access the device.</p>");
    page += F("</div>");
    page += FPSTR(HTML_FOOTER);

    return page;
}
