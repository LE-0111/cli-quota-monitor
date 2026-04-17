#include "ui_renderer.h"
#include <time.h>
#include <string.h>

namespace UIRenderer {

static constexpr float WARN_THRESHOLD_PCT = 80.0f;
static constexpr uint32_t FRESH_SEC = 6UL * 3600UL;

// ============================================================
// Time / duration formatting
// ============================================================

void formatDuration(uint32_t resetEpoch, uint32_t nowEpoch,
                    char* buf, size_t len) {
    if (resetEpoch == 0 || nowEpoch == 0 || resetEpoch <= nowEpoch) {
        snprintf(buf, len, "--");
        return;
    }
    uint32_t secs = resetEpoch - nowEpoch;
    uint32_t days = secs / 86400;
    uint32_t hours = (secs % 86400) / 3600;
    uint32_t mins = (secs % 3600) / 60;

    if (days > 0)       snprintf(buf, len, "%lud%luh", (unsigned long)days, (unsigned long)hours);
    else if (hours > 0) snprintf(buf, len, "%luh%02lum", (unsigned long)hours, (unsigned long)mins);
    else                snprintf(buf, len, "%lum", (unsigned long)mins);
}

void formatLastUpdate(uint32_t epoch, char* buf, size_t len) {
    if (epoch == 0) { snprintf(buf, len, "--:--"); return; }
    time_t t = (time_t)epoch;
    struct tm tmCopy;
    if (!localtime_r(&t, &tmCopy)) { snprintf(buf, len, "--:--"); return; }
    strftime(buf, len, "%H:%M", &tmCopy);
}

static void formatDateOnly(uint32_t epoch, char* buf, size_t len) {
    if (epoch == 0) { snprintf(buf, len, "----/--/--"); return; }
    time_t t = (time_t)epoch;
    struct tm tmCopy;
    if (!localtime_r(&t, &tmCopy)) { snprintf(buf, len, "----/--/--"); return; }
    strftime(buf, len, "%Y-%m-%d", &tmCopy);
}

// Compact form: HH:MM (today) / Wed HH:MM (within a week) / MM-DD (further)
static void formatResetCompact(uint32_t resetEpoch, uint32_t now,
                               char* buf, size_t len) {
    if (resetEpoch == 0 || now == 0 || resetEpoch <= now) {
        snprintf(buf, len, "--");
        return;
    }
    time_t t = (time_t)resetEpoch;
    struct tm tmCopy;
    if (!localtime_r(&t, &tmCopy)) { snprintf(buf, len, "--"); return; }
    uint32_t diff = resetEpoch - now;
    if (diff < 24UL * 3600UL)
        strftime(buf, len, "%H:%M", &tmCopy);
    else if (diff < 7UL * 24UL * 3600UL)
        strftime(buf, len, "%a %H:%M", &tmCopy);
    else
        strftime(buf, len, "%m-%d", &tmCopy);
}

// Full form: "YYYY-MM-DD HH:MM"
static void formatResetFull(uint32_t resetEpoch, uint32_t now,
                            char* buf, size_t len) {
    if (resetEpoch == 0 || resetEpoch <= now) {
        snprintf(buf, len, "--");
        return;
    }
    time_t t = (time_t)resetEpoch;
    struct tm tmCopy;
    if (!localtime_r(&t, &tmCopy)) { snprintf(buf, len, "--"); return; }
    strftime(buf, len, "%Y-%m-%d %H:%M", &tmCopy);
}

// ============================================================
// Freshness — based on last_update timestamp, not plan string
// ============================================================

enum Freshness { FRESH_FRESH, FRESH_STALE, FRESH_NONE };

static Freshness computeFreshness(const ServiceUsage& svc) {
    if (svc.last_update_epoch == 0) return FRESH_NONE;
    uint32_t now = (uint32_t)time(nullptr);
    if (now < 1600000000UL) return FRESH_NONE;
    uint32_t age = now - svc.last_update_epoch;
    return (age < FRESH_SEC) ? FRESH_FRESH : FRESH_STALE;
}

static void drawFreshnessDot(Adafruit_GFX& d, int16_t cx, int16_t cy,
                             const ServiceUsage& svc) {
    Freshness f = computeFreshness(svc);
    if (f == FRESH_FRESH)       d.fillCircle(cx, cy, 3, UI_COLOR_FG);
    else if (f == FRESH_STALE)  d.drawCircle(cx, cy, 3, UI_COLOR_FG);
    // FRESH_NONE: nothing
}

// ============================================================
// Progress bar
// ============================================================

void drawProgressBar(Adafruit_GFX& d, int16_t x, int16_t y,
                     int16_t w, int16_t h, float percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    d.drawRect(x, y, w, h, UI_COLOR_FG);
    int16_t fillW = (int16_t)((w - 2) * (percent / 100.0f));
    if (fillW > 0) d.fillRect(x + 1, y + 1, fillW, h - 2, UI_COLOR_FG);
}

static void drawLabelMaybeInverted(Adafruit_GFX& d, int16_t x, int16_t y,
                                   const char* text, bool warn) {
    if (warn) {
        int16_t bx, by;
        uint16_t bw, bh;
        d.getTextBounds(text, x, y, &bx, &by, &bw, &bh);
        d.fillRect(bx - 2, by - 1, bw + 4, bh + 2, UI_COLOR_FG);
        d.setTextColor(UI_COLOR_BG);
    } else {
        d.setTextColor(UI_COLOR_FG);
    }
    d.setCursor(x, y);
    d.print(text);
    d.setTextColor(UI_COLOR_FG);
}

void drawHeader(Adafruit_GFX& d, const char* title, int16_t y, int16_t w) {
    d.setTextColor(UI_COLOR_FG);
    d.setTextSize(1);
    d.setCursor(4, y);
    d.print(title);
    d.drawFastHLine(0, y + 10, w, UI_COLOR_FG);
}

// ============================================================
// Wide Claude panel (400 × 140)
//
//  Claude ●             Pro   Upd 2026-04-16 11:19
//  ─────────────────────────────────────────────────
//  5h    24%  [██░░░░░░░░░░░░░]    reset 16:00
//  week  29%  [██░░░░░░░░░░░░░]    reset 2026-04-22 14:00
// ============================================================

static void drawServiceWide(Adafruit_GFX& d, const char* title,
                            const ServiceUsage& svc,
                            int16_t x, int16_t y, int16_t w, int16_t h) {
    const int16_t PAD = 6;
    int16_t cy = y + 4;

    // Panel border
    d.drawRect(x, y, w, h, UI_COLOR_FG);

    // Title (size 2, left)
    d.setTextSize(2);
    d.setTextColor(UI_COLOR_FG);
    d.setCursor(x + PAD, cy);
    d.print(title);
    int16_t bx; int16_t by; uint16_t tw, th;
    d.getTextBounds(title, 0, 0, &bx, &by, &tw, &th);

    // Freshness dot right of title
    drawFreshnessDot(d, x + PAD + tw + 10, cy + th / 2, svc);

    // Right side: Plan + update date/time (size 1)
    d.setTextSize(1);
    char dateBuf[12], timeBuf[8], rightBuf[48];
    formatDateOnly(svc.last_update_epoch, dateBuf, sizeof(dateBuf));
    formatLastUpdate(svc.last_update_epoch, timeBuf, sizeof(timeBuf));
    snprintf(rightBuf, sizeof(rightBuf), "%s   Upd %s %s",
             svc.plan, dateBuf, timeBuf);
    uint16_t rw, rh;
    d.getTextBounds(rightBuf, 0, 0, &bx, &by, &rw, &rh);
    d.setCursor(x + w - PAD - rw, cy + 4);
    d.print(rightBuf);

    cy += th + 6;
    d.drawFastHLine(x + PAD, cy, w - PAD * 2, UI_COLOR_FG);
    cy += 8;

    // Data rows
    uint32_t now = (uint32_t)time(nullptr);

    // Fixed column geometry
    const int16_t pctX = x + PAD + 70;       // percent text column
    const int16_t barX = pctX + 70;          // bar column start
    const int16_t barW = x + w - PAD - barX; // bar extends to right edge

    auto drawDataRow = [&](const char* label, const RateLimitWindow& win,
                           bool fullDate) {
        // Line 1: label + pct + bar
        d.setTextSize(2);
        d.setTextColor(UI_COLOR_FG);
        d.setCursor(x + PAD, cy);
        d.print(label);

        char pctBuf[16];
        if (win.present) snprintf(pctBuf, sizeof(pctBuf), "%3.0f%%", win.used_percentage);
        else             snprintf(pctBuf, sizeof(pctBuf), " --");
        bool warn = win.present && win.used_percentage >= WARN_THRESHOLD_PCT;
        drawLabelMaybeInverted(d, pctX, cy, pctBuf, warn);

        if (barW > 20) {
            drawProgressBar(d, barX, cy + 3, barW, 12,
                            win.present ? win.used_percentage : 0);
        }
        cy += 18;

        // Line 2: reset (size 1), aligned under the bar
        d.setTextSize(1);
        d.setTextColor(UI_COLOR_FG);
        char resetBuf[40];
        char absBuf[28];
        if (fullDate) formatResetFull(win.resets_at, now, absBuf, sizeof(absBuf));
        else          formatResetCompact(win.resets_at, now, absBuf, sizeof(absBuf));
        snprintf(resetBuf, sizeof(resetBuf), "reset %s", absBuf);
        d.setCursor(barX, cy);
        d.print(resetBuf);
        cy += 12;
    };

    drawDataRow("5h",   svc.five_hour, false);
    cy += 4;
    drawDataRow("week", svc.seven_day, true);
}

// ============================================================
// Compact panel (200 × 140), vertical stack
// ============================================================

static void drawServiceCompact(Adafruit_GFX& d, const char* title,
                               const ServiceUsage& svc,
                               int16_t x, int16_t y, int16_t w, int16_t h,
                               const char* labelA = "5h",
                               const char* labelB = "week",
                               bool fullDateB = true) {
    const int16_t PAD = 4;
    int16_t cy = y + 3;

    d.drawRect(x, y, w, h, UI_COLOR_FG);

    // Title (size 2, centered) + dot
    d.setTextSize(2);
    d.setTextColor(UI_COLOR_FG);
    int16_t bx, by; uint16_t tw, th;
    d.getTextBounds(title, 0, 0, &bx, &by, &tw, &th);
    int16_t titleX = x + (w - tw) / 2;
    d.setCursor(titleX, cy);
    d.print(title);
    drawFreshnessDot(d, titleX + tw + 8, cy + th / 2, svc);
    cy += th + 3;

    d.drawFastHLine(x + PAD, cy, w - PAD * 2, UI_COLOR_FG);
    cy += 4;

    d.setTextSize(1);
    d.setTextColor(UI_COLOR_FG);

    // Plan + Updated date/time on one line:  "Pro · 2026-04-16 11:19"
    char dateBuf[12], timeBuf[8], planLine[40];
    formatDateOnly(svc.last_update_epoch, dateBuf, sizeof(dateBuf));
    formatLastUpdate(svc.last_update_epoch, timeBuf, sizeof(timeBuf));
    snprintf(planLine, sizeof(planLine), "%s  %s %s",
             svc.plan, dateBuf, timeBuf);
    d.setCursor(x + PAD, cy);
    d.print(planLine);
    cy += 12;

    uint32_t now = (uint32_t)time(nullptr);
    char pctBuf[16], resetBuf[40], absBuf[28];

    auto drawBlock = [&](const char* label, const RateLimitWindow& win,
                         bool fullDate) {
        if (win.present) snprintf(pctBuf, sizeof(pctBuf), "%s %3.0f%%", label, win.used_percentage);
        else             snprintf(pctBuf, sizeof(pctBuf), "%s  --", label);
        bool warn = win.present && win.used_percentage >= WARN_THRESHOLD_PCT;
        drawLabelMaybeInverted(d, x + PAD, cy, pctBuf, warn);
        cy += 10;

        drawProgressBar(d, x + PAD, cy, w - PAD * 2, 8,
                        win.present ? win.used_percentage : 0);
        cy += 11;

        if (fullDate) formatResetFull(win.resets_at, now, absBuf, sizeof(absBuf));
        else          formatResetCompact(win.resets_at, now, absBuf, sizeof(absBuf));
        snprintf(resetBuf, sizeof(resetBuf), "reset %s", absBuf);
        d.setCursor(x + PAD, cy);
        d.print(resetBuf);
        cy += 12;
    };

    drawBlock(labelA, svc.five_hour, false);
    cy += 2;
    drawBlock(labelB, svc.seven_day, fullDateB);
}

// Legacy API wrapper (pick layout by width)
void drawServicePanel(Adafruit_GFX& d, const char* title,
                      const ServiceUsage& svc,
                      int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w >= 300) drawServiceWide(d, title, svc, x, y, w, h);
    else          drawServiceCompact(d, title, svc, x, y, w, h);
}

// ============================================================
// Top status bar
// ============================================================

void drawStatusBar(Adafruit_GFX& d, const AppState& state,
                   int16_t y, int16_t w) {
    d.setTextColor(UI_COLOR_FG);
    d.setTextSize(1);

    d.setCursor(4, y + 5);
    d.print("AI CLI Monitor");

    char timeBuf[8];
    uint32_t now = (uint32_t)time(nullptr);
    if (now > 1600000000UL) {
        time_t t = (time_t)now;
        struct tm tmCopy;
        if (localtime_r(&t, &tmCopy))
            strftime(timeBuf, sizeof(timeBuf), "%H:%M", &tmCopy);
        else
            snprintf(timeBuf, sizeof(timeBuf), "--:--");
    } else {
        snprintf(timeBuf, sizeof(timeBuf), "--:--");
    }

    const char* wifiStr   = state.wifi_connected ? "WiFi OK"   : "WiFi --";
    const char* bridgeStr = state.bridge_error   ? "Bridge --" : "Bridge OK";

    char right[48];
    snprintf(right, sizeof(right), "%s  %s  %s", wifiStr, bridgeStr, timeBuf);

    int16_t bx, by; uint16_t tw, th;
    d.getTextBounds(right, 0, 0, &bx, &by, &tw, &th);
    d.setCursor(w - tw - 4, y + 5);
    d.print(right);
}

// ============================================================
// Dashboard layout: Claude top (400×140), Codex bottom-left,
// Gemini bottom-right (200×139 each)
// ============================================================

void drawDashboard(Adafruit_GFX& d, const AppState& state) {
    int16_t W = d.width();   // 400
    int16_t H = d.height();  // 300

    d.fillScreen(UI_COLOR_BG);
    d.setTextColor(UI_COLOR_FG);

    const int16_t HEADER_H = 20;
    drawStatusBar(d, state, 0, W);
    d.drawFastHLine(0, HEADER_H, W, UI_COLOR_FG);

    // Top panel: Claude full-width
    const int16_t topY = HEADER_H + 2;
    const int16_t topH = 138;
    drawServiceWide(d, "Claude", state.services[SERVICE_CLAUDE],
                    0, topY, W, topH);

    // Bottom-left: Codex
    const int16_t bottomY = topY + topH + 2;
    const int16_t bottomH = H - bottomY;
    const int16_t halfW = W / 2;
    drawServiceCompact(d, "Codex", state.services[SERVICE_CODEX],
                       0, bottomY, halfW, bottomH);

    // Bottom-right: Gemini — two rows are per-model daily quota, not 5h/week.
    drawServiceCompact(d, "Gemini", state.services[SERVICE_GEMINI],
                       halfW, bottomY, W - halfW, bottomH,
                       "Flash", "Pro", false);

    // Fetching indicator
    if (state.is_fetching) {
        d.fillCircle(W - 4, 3, 2, UI_COLOR_FG);
    }

    // Error overlay bottom (overlays Codex/Gemini bottom edges)
    if (state.bridge_error && state.last_error[0] != '\0') {
        d.setTextSize(1);
        d.fillRect(0, H - 10, W, 10, UI_COLOR_FG);
        d.setTextColor(UI_COLOR_BG);
        d.setCursor(4, H - 9);
        d.print("ERR: ");
        d.print(state.last_error);
        d.setTextColor(UI_COLOR_FG);
    }
}

// ============================================================
// Status diagnostic screen
// ============================================================

void drawStatus(Adafruit_GFX& d, const AppState& state) {
    int16_t W = d.width();

    d.fillScreen(UI_COLOR_BG);
    d.setTextColor(UI_COLOR_FG);

    d.setTextSize(2);
    d.setCursor(8, 8);
    d.print("STATUS");
    d.drawFastHLine(8, 30, W - 16, UI_COLOR_FG);

    d.setTextSize(1);
    int16_t y = 40;
    int16_t lineH = 12;

    d.setCursor(8, y); d.printf("WiFi:   %s", state.wifi_connected ? "Connected" : "Disconnected"); y += lineH;
    d.setCursor(8, y); d.printf("SSID:   %s", state.wifi_ssid); y += lineH;
    d.setCursor(8, y); d.printf("IP:     %s", state.wifi_ip); y += lineH;
    d.setCursor(8, y); d.printf("RSSI:   %d dBm", state.wifi_rssi); y += lineH;
    d.setCursor(8, y); d.printf("Uptime: %lu s", (millis() - state.uptime_start) / 1000); y += lineH;

    y += 4;
    d.setCursor(8, y); d.printf("Bridge: %s", state.bridge_error ? "ERROR" : "OK"); y += lineH;
    if (state.bridge_error) {
        d.setCursor(8, y); d.printf("Error:  %s", state.last_error); y += lineH;
    }

    y += 4;
    const char* names[] = {"Claude", "Codex ", "Gemini"};
    for (int i = 0; i < SERVICE_COUNT; i++) {
        d.setCursor(8, y);
        d.printf("%s: %s", names[i], state.services[i].plan);
        y += lineH;
    }
}

} // namespace UIRenderer
