#pragma once

#include <Adafruit_GFX.h>
#include "display_bsp.h"

/*
 * Adafruit_GFX adapter for the Waveshare ESP32-S3-RLCD-4.2 board.
 * This is a 400x300 monochrome reflective LCD. The underlying driver
 * (DisplayPort) exposes RLCD_SetPixel(x, y, 0|0xFF) and RLCD_Display()
 * to push the framebuffer.
 *
 * We treat any Adafruit_GFX color with its high bit set as "white",
 * everything else as "black" — matching the threshold used in the
 * vendor LVGL example.
 *
 * Pins (from vendor demo): MOSI=12, SCL=11, DC=5, CS=40, RST=41
 */
class WaveshareRLCDGfx : public Adafruit_GFX {
public:
    WaveshareRLCDGfx()
      : Adafruit_GFX(400, 300),
        port_(12, 11, 5, 40, 41, 400, 300) {}

    void begin() {
        port_.RLCD_Init();
        port_.RLCD_ColorClear(ColorWhite);
        port_.RLCD_Display();
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= 400 || y >= 300) return;
        // Map RGB565: 0x0000 (black) -> 0, 0xFFFF (white) -> 0xFF.
        // Threshold at 0x7FFF so any mid-grey rounds to white like the demo.
        uint8_t c = (color >= 0x7FFF) ? ColorWhite : ColorBlack;
        port_.RLCD_SetPixel((uint16_t)x, (uint16_t)y, c);
    }

    void fillScreen(uint16_t color) override {
        uint8_t c = (color >= 0x7FFF) ? ColorWhite : ColorBlack;
        port_.RLCD_ColorClear(c);
    }

    // Push the framebuffer to the panel. Call after drawing a frame.
    void display() {
        port_.RLCD_Display();
    }

private:
    DisplayPort port_;
};
