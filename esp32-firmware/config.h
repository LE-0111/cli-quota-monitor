#pragma once

// ============================================================
// Bridge Server (Python HTTP server running on your Mac)
// ============================================================
#define BRIDGE_DEFAULT_PORT    8899
#define BRIDGE_ENDPOINT        "/usage"

// ============================================================
// WiFi
// ============================================================
#define WIFI_TIMEOUT_MS 15000

// ============================================================
// Refresh Settings
// ============================================================
#define REFRESH_INTERVAL_MS  30000    // 30 seconds (local network, cheap to poll)
#define NTP_SERVER           "pool.ntp.org"
#define NTP_SYNC_INTERVAL_MS 3600000  // Re-sync NTP every 1 hour

// ============================================================
// Backlight Settings
// ============================================================
#define BACKLIGHT_FULL       255
#define BACKLIGHT_DIM        40
#define DIM_TIMEOUT_MS       120000   // Dim after 2 minutes

// ============================================================
// Reboot delay after web config save (ms)
// ============================================================
#define REBOOT_DELAY_MS      2000

// ============================================================
// Misc
// ============================================================
#define ERROR_MSG_LEN        64
#define IP_LEN               16
