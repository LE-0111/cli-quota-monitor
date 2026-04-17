#pragma once

#include <Adafruit_GFX.h>

/*
 * NullDisplay — a no-op Adafruit_GFX implementation so the sketch
 * compiles and runs (with data flowing to Serial) before you wire
 * in a real display driver.
 *
 * REPLACE this with your actual display driver (Arduino_GFX /
 * GxEPD2 / Adafruit_ST7789 etc.) and remove this include.
 */
class NullDisplay : public Adafruit_GFX {
public:
    NullDisplay(int16_t w, int16_t h) : Adafruit_GFX(w, h) {}

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        (void)x; (void)y; (void)color;
    }

    // Stub methods — most do nothing. Keeps the compiler happy and lets the
    // UI renderer produce serial log lines when pen-down.
    void fillScreen(uint16_t color) override { (void)color; }
};
