#pragma once

#include <Adafruit_GFX.h>
#include "data_models.h"

// UI colors. Override in config.h if needed.
// Defaults work for both monochrome (e-paper) and color displays by mapping
// black/white. For color TFTs, redefine these as RGB565 values.
#ifndef UI_COLOR_BG
  #define UI_COLOR_BG      0xFFFF   // White
#endif
#ifndef UI_COLOR_FG
  #define UI_COLOR_FG      0x0000   // Black
#endif
#ifndef UI_COLOR_ACCENT
  #define UI_COLOR_ACCENT  0x0000   // Same as FG (override with RGB565 for color TFT)
#endif
#ifndef UI_COLOR_MUTED
  #define UI_COLOR_MUTED   0x8410   // Grey (approx)
#endif

namespace UIRenderer {

    // Draw the complete dashboard (all 3 services + status bar).
    // display: any Adafruit_GFX-compatible display (GxEPD2, Arduino_GFX, etc.)
    //          For TFT_eSPI users, wrap with an Adafruit_GFX adapter.
    void drawDashboard(Adafruit_GFX& display, const AppState& state);

    // Draw diagnostic/status screen (WiFi details, uptime, errors).
    void drawStatus(Adafruit_GFX& display, const AppState& state);

    // Helpers (usable by custom screens)
    void drawServicePanel(Adafruit_GFX& d, const char* title,
                          const ServiceUsage& svc,
                          int16_t x, int16_t y, int16_t w, int16_t h);
    void drawProgressBar(Adafruit_GFX& d, int16_t x, int16_t y,
                         int16_t w, int16_t h, float percent);
    void drawHeader(Adafruit_GFX& d, const char* title, int16_t y, int16_t w);
    void drawStatusBar(Adafruit_GFX& d, const AppState& state,
                       int16_t y, int16_t w);

    // Format helpers
    void formatDuration(uint32_t resetEpoch, uint32_t nowEpoch,
                        char* buf, size_t len);
    void formatLastUpdate(uint32_t epoch, char* buf, size_t len);
}
