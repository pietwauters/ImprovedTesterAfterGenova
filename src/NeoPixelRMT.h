// Ported unchanged from esp32scoringdeviceMqtt's NeoPixelRMT (same sibling
// platform, same WiFi + AsyncWebServer load) - timing constants below are the
// ones that were tuned against real hardware there. Don't touch T0H/T0L/T1H/T1L
// without re-verifying on a scope; that's the whole point of reusing this file
// instead of trusting a library's internal timing.
#pragma once
#include <Arduino.h>  // pulled in transitively by WS2812BLedMatrix.cpp: pinMode/digitalWrite/delay/millis/HIGH/LOW/OUTPUT

#include <algorithm>
#include <vector>

#include "driver/rmt.h"

class NeoPixelRMT {
   public:
    NeoPixelRMT(uint16_t numPixels, gpio_num_t pin);
    ~NeoPixelRMT();

    void begin();

    void setPixelColor(uint16_t idx, uint32_t color);
    void fill(uint32_t color, int startIndex, int count);
    void clear();
    void show();

    // No-op: brightness is baked into colours by Color(...,brightness) below,
    // same as in the scoring-device original. Kept only so callers that expect
    // an Adafruit-style setBrightness() still compile.
    void setBrightness(uint8_t /*brightness*/) {}

    static uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness = 255);

   private:
    void encodePixels();
    void setRMTConfig();

    static constexpr int BITS_PER_PIXEL = 24;

    uint16_t numPixels;
    gpio_num_t pin;
    rmt_channel_t channel;

    std::vector<uint32_t> pixels;     // Pixels in GRB order packed
    std::vector<rmt_item32_t> items;  // RMT waveform data

    // Timing parameters (clock ticks at clk_div=2, 40MHz tick = 25ns)
    static constexpr int T0H = 14;        // 350ns
    static constexpr int T0L = 36;        // 950ns
    static constexpr int T1H = 36;        // 700ns
    static constexpr int T1L = 14;        // 600ns
    static constexpr int RESET_US = 150;  // Reset pulse length
};
