/*
 * CC Display — Claude Code / Codex / Gemini Usage Monitor
 * Adapted from ClaudeGauge. Uses Adafruit_GFX-compatible display driver.
 *
 * Hardware target: Waveshare ESP32-S3-RLCD-4.2
 * IDE: Arduino IDE with ESP32 board support
 *
 * Required libraries:
 *   - ArduinoJson (>= 7.3.0)
 *   - Adafruit GFX Library
 *   - One display driver that inherits from Adafruit_GFX:
 *       * Arduino_GFX_Library  — for most Waveshare LCDs
 *       * GxEPD2               — for e-paper displays
 *       * Adafruit_ST7789 etc. — for generic SPI TFTs
 *
 * BEFORE FLASHING: pick ONE of the display init blocks below
 * (search for "DISPLAY DRIVER SETUP").
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>

#include "config.h"
#include "data_models.h"
#include "wifi_manager.h"
#include "usage_client.h"
#include "settings_manager.h"
#include "web_server.h"
#include "ui_renderer.h"

// ============================================================
// ============  DISPLAY DRIVER SETUP (PICK ONE)  =============
// ============================================================
// Comment out the others. The only requirement is that the global
// pointer `gfx` points to an Adafruit_GFX-compatible instance.
// ============================================================

// ---------- Option A: Arduino_GFX (Waveshare LCDs, most common) ----------
// Uncomment and adjust pins for your board (check Waveshare wiki).
//
// #include <Arduino_GFX_Library.h>
// Arduino_DataBus* bus = new Arduino_ESP32SPI(7 /*DC*/, 10 /*CS*/, 12 /*SCK*/, 11 /*MOSI*/);
// Arduino_GFX* gfx = new Arduino_ST7789(bus, 8 /*RST*/, 0 /*rotation*/,
//                                       true /*IPS*/, 320, 240);

// ---------- Option B: GxEPD2 (e-paper displays) ----------
// #include <GxEPD2_BW.h>
// GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT>
//     display(GxEPD2_420_GDEY042T81(/*CS=*/7, /*DC=*/8, /*RST=*/9, /*BUSY=*/3));
// Adafruit_GFX* gfx = &display;

// ---------- Option D: Waveshare ESP32-S3-RLCD-4.2 (400x300 monochrome reflective LCD) ----------
#include "gfx_waveshare_rlcd.h"
static WaveshareRLCDGfx rlcdGfx;
Adafruit_GFX* gfx = &rlcdGfx;
#define CCD_DISPLAY_NEEDS_FLUSH 1

// ============================================================
// Device modes
// ============================================================
enum DeviceMode { MODE_SETUP, MODE_CONNECTING, MODE_DASHBOARD };

// ============================================================
// Global instances
// ============================================================
static AppState        state;
static WiFiManager     wifiMgr;
static UsageClient     usageClient;
static SettingsManager settingsMgr;
static ConfigWebServer webServer;
static DeviceMode      deviceMode = MODE_SETUP;

// ============================================================
// Forward declarations
// ============================================================
void enterSetupMode();
void enterDashboardMode();
void fetchUsageData();
void drawCurrentScreen();
void handleAutoRefresh();
void updateWiFiState();
void syncNTPBestEffort();

// ============================================================
// Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== CC Display v1.0 ===");

    memset(&state, 0, sizeof(AppState));
    state.current_screen = SCREEN_DASHBOARD;
    state.backlight_level = BACKLIGHT_FULL;
    state.uptime_start = millis();
    state.last_activity = millis();
    for (int i = 0; i < SERVICE_COUNT; i++) {
        strncpy(state.services[i].plan, "NO DATA", PLAN_STR_LEN - 1);
    }

    rlcdGfx.begin();
    gfx->fillScreen(UI_COLOR_BG);

    // Splash screen
    gfx->setTextColor(UI_COLOR_FG);
    gfx->setTextSize(2);
    gfx->setCursor(10, 20);
    gfx->print("CC Display");
    gfx->setTextSize(1);
    gfx->setCursor(10, 50);
    gfx->print("Booting...");
#ifdef CCD_DISPLAY_NEEDS_FLUSH
    rlcdGfx.display();
#endif

    settingsMgr.begin();

    if (!settingsMgr.hasWiFi()) {
        Serial.println("No WiFi credentials. Entering setup mode.");
        enterSetupMode();
        return;
    }

    Serial.println("Configuration found. Connecting...");
    deviceMode = MODE_CONNECTING;

    String ssid = settingsMgr.getWiFiSSID();
    String pass = settingsMgr.getWiFiPassword();
    strncpy(state.wifi_ssid, ssid.c_str(), sizeof(state.wifi_ssid) - 1);

    gfx->setCursor(10, 70);
    gfx->print("WiFi: ");
    gfx->print(ssid);

    wifiMgr.connect(ssid, pass);
    updateWiFiState();

    if (!wifiMgr.isConnected()) {
        Serial.println("WiFi failed. Entering setup mode.");
        enterSetupMode();
        return;
    }

    webServer.begin(&settingsMgr);

    syncNTPBestEffort();

    if (settingsMgr.hasBridge()) {
        usageClient.init(settingsMgr.getBridgeHost(), settingsMgr.getBridgePort());
        Serial.printf("Bridge: %s:%d\n",
                      settingsMgr.getBridgeHost().c_str(),
                      settingsMgr.getBridgePort());
        fetchUsageData();
    } else {
        Serial.println("No bridge server configured.");
        state.bridge_error = true;
        snprintf(state.last_error, ERROR_MSG_LEN,
                 "No bridge - config at %s", wifiMgr.getIP().c_str());
    }

    enterDashboardMode();
}

// ============================================================
// Setup mode (AP + captive portal)
// ============================================================
void enterSetupMode() {
    deviceMode = MODE_SETUP;

    webServer.startAPMode();
    webServer.begin(&settingsMgr);

    String apName = webServer.getAPName();
    String apIP = WiFi.softAPIP().toString();

    Serial.printf("Setup: join '%s', open http://%s\n",
                  apName.c_str(), apIP.c_str());

    gfx->fillScreen(UI_COLOR_BG);
    gfx->setTextColor(UI_COLOR_FG);

    gfx->setTextSize(2);
    gfx->setCursor(10, 20);
    gfx->print("SETUP MODE");

    gfx->setTextSize(1);
    gfx->setCursor(10, 60);
    gfx->print("1. Join WiFi:");
    gfx->setTextSize(2);
    gfx->setCursor(10, 78);
    gfx->print(apName);

    gfx->setTextSize(1);
    gfx->setCursor(10, 110);
    gfx->print("2. Open:");
    gfx->setTextSize(2);
    gfx->setCursor(10, 128);
    gfx->print("http://");
    gfx->print(apIP);
#ifdef CCD_DISPLAY_NEEDS_FLUSH
    rlcdGfx.display();
#endif
}

// ============================================================
// Dashboard mode
// ============================================================
void enterDashboardMode() {
    deviceMode = MODE_DASHBOARD;
    drawCurrentScreen();
    Serial.println("Dashboard mode active.");
}

// ============================================================
// Main loop
// ============================================================
void loop() {
    webServer.handleClient();

    if (webServer.shouldReboot()) {
        Serial.println("Settings saved. Rebooting...");
        delay(REBOOT_DELAY_MS);
        ESP.restart();
    }

    if (deviceMode == MODE_SETUP) {
        delay(10);
        return;
    }

    handleAutoRefresh();

    if (!wifiMgr.isConnected()) {
        wifiMgr.reconnectIfNeeded();
        updateWiFiState();
    }

    delay(10);
}

// ============================================================
// Fetch usage from bridge server
// ============================================================
void fetchUsageData() {
    if (!settingsMgr.hasBridge()) return;
    if (!wifiMgr.isConnected()) {
        Serial.println("Skip fetch: WiFi disconnected");
        return;
    }

    state.is_fetching = true;
    strncpy(state.fetch_status, "Fetching...", sizeof(state.fetch_status) - 1);
    drawCurrentScreen();

    Serial.println("--- Fetching usage data ---");

    ServiceUsage newServices[SERVICE_COUNT];
    int32_t serverTs = 0;

    if (usageClient.fetchAll(newServices, serverTs)) {
        for (int i = 0; i < SERVICE_COUNT; i++) {
            state.services[i] = newServices[i];
        }
        state.bridge_timestamp = serverTs;
        state.bridge_error = false;
        state.last_error[0] = '\0';

        Serial.printf("Claude: 5h=%.1f%% 7d=%.1f%% plan=%s\n",
                      state.services[SERVICE_CLAUDE].five_hour.used_percentage,
                      state.services[SERVICE_CLAUDE].seven_day.used_percentage,
                      state.services[SERVICE_CLAUDE].plan);
        Serial.printf("Codex:  5h=%.1f%% 7d=%.1f%% plan=%s\n",
                      state.services[SERVICE_CODEX].five_hour.used_percentage,
                      state.services[SERVICE_CODEX].seven_day.used_percentage,
                      state.services[SERVICE_CODEX].plan);
        Serial.printf("Gemini: 5h=%.1f%% 7d=%.1f%% plan=%s\n",
                      state.services[SERVICE_GEMINI].five_hour.used_percentage,
                      state.services[SERVICE_GEMINI].seven_day.used_percentage,
                      state.services[SERVICE_GEMINI].plan);
    } else {
        state.bridge_error = true;
        strncpy(state.last_error, usageClient.getLastError(), ERROR_MSG_LEN - 1);
        Serial.printf("Fetch failed: %s\n", state.last_error);
    }

    state.is_fetching = false;
    state.last_refresh = millis();
    state.next_refresh = state.last_refresh + REFRESH_INTERVAL_MS;

    Serial.println("--- Fetch complete ---");
    drawCurrentScreen();
}

// ============================================================
// Screen drawing
// ============================================================
void drawCurrentScreen() {
    switch (state.current_screen) {
        case SCREEN_DASHBOARD:
            UIRenderer::drawDashboard(*gfx, state);
            break;
        case SCREEN_STATUS:
            UIRenderer::drawStatus(*gfx, state);
            break;
        default:
            UIRenderer::drawDashboard(*gfx, state);
            break;
    }

#ifdef CCD_DISPLAY_NEEDS_FLUSH
    rlcdGfx.display();
#endif
}

// ============================================================
// Auto-refresh
// ============================================================
void handleAutoRefresh() {
    uint32_t now = millis();
    if (state.next_refresh > 0 && now >= state.next_refresh) {
        Serial.println("Auto-refresh triggered");
        fetchUsageData();
    }

    if (state.last_refresh == 0 && wifiMgr.isConnected() &&
        settingsMgr.hasBridge() && !state.is_fetching) {
        fetchUsageData();
    }
}

// ============================================================
// Helpers
// ============================================================
void updateWiFiState() {
    state.wifi_connected = wifiMgr.isConnected();
    state.wifi_rssi = wifiMgr.getRSSI();
    String ip = wifiMgr.getIP();
    strncpy(state.wifi_ip, ip.c_str(), IP_LEN - 1);
    state.wifi_ip[IP_LEN - 1] = '\0';

    String ssid = settingsMgr.getWiFiSSID();
    strncpy(state.wifi_ssid, ssid.c_str(), sizeof(state.wifi_ssid) - 1);
    state.wifi_ssid[sizeof(state.wifi_ssid) - 1] = '\0';
}

void syncNTPBestEffort() {
    // CST-8: China Standard Time, UTC+8, no DST
    configTzTime("CST-8", NTP_SERVER);
    Serial.print("Syncing NTP");
    uint32_t start = millis();
    while (time(nullptr) < 1600000000 && millis() - start < 5000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();
}
