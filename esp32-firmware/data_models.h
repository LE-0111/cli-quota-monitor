#pragma once

#include <Arduino.h>
#include "config.h"

// ==================== Rate Limit Window ====================

struct RateLimitWindow {
    float    used_percentage;   // 0-100
    uint32_t resets_at;         // Unix epoch seconds (0 if unknown)
    bool     present;           // false if data not available
};

// ==================== Service Usage ====================

#define PLAN_STR_LEN   16

struct ServiceUsage {
    RateLimitWindow five_hour;
    RateLimitWindow seven_day;
    char            plan[PLAN_STR_LEN];     // "ACTIVE", "NO DATA", etc.
    uint32_t        last_update_epoch;      // server-side epoch of last data point
    bool            valid;
    uint32_t        fetched_at;             // ESP32 millis() timestamp
};

// ==================== Service IDs ====================

enum ServiceID : uint8_t {
    SERVICE_CLAUDE = 0,
    SERVICE_CODEX  = 1,
    SERVICE_GEMINI = 2,
    SERVICE_COUNT  = 3
};

// ==================== Screen IDs ====================

enum ScreenID : uint8_t {
    SCREEN_DASHBOARD  = 0,   // All 3 services on one screen
    SCREEN_STATUS     = 1,   // WiFi / bridge diagnostics
    SCREEN_COUNT      = 2
};

// ==================== App State ====================

struct AppState {
    ScreenID       current_screen;

    ServiceUsage   services[SERVICE_COUNT];  // Claude, Codex, Gemini

    // WiFi status
    bool           wifi_connected;
    int16_t        wifi_rssi;
    char           wifi_ip[IP_LEN];
    char           wifi_ssid[33];

    // Bridge status
    bool           bridge_error;
    char           last_error[ERROR_MSG_LEN];
    int32_t        bridge_timestamp;         // server epoch from last response

    // Timing
    uint32_t       last_refresh;             // millis()
    uint32_t       next_refresh;             // millis()
    uint32_t       uptime_start;

    // Display
    uint8_t        backlight_level;
    uint32_t       last_activity;
    bool           is_fetching;
    char           fetch_status[32];
};
