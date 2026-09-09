// Copyright (c) Piet Wauters 2022 <piet.wauters@gmail.com>
//
// DMA (I2S) WS2812 output with an Adafruit_NeoPixel-compatible surface, so
// WS2812BLedMatrix keeps working with only its #include and member type changed.
//
// Rationale: the RMT path in Adafruit_NeoPixel refills a 64-symbol buffer from an
// ISR-driven translator while the frame plays. When WiFi is torn down (flash
// write + interrupt-matrix reconfiguration) that refill starves and the frame
// corrupts; with no periodic re-send the bad frame stays lit and can cook an LED.
// I2S clocks the whole frame out of RAM via DMA - once Show() starts, the CPU,
// interrupts and flash writes are irrelevant.
//
// I2S peripheral 1 on purpose: I2S0 on the classic ESP32 shares hardware with the
// ADC continuous driver, and this firmware leans hard on ADC1.
#pragma once
#include <Arduino.h>
#include <NeoPixelBus.h>

#include <vector>

class NeoPixelI2S {
   public:
    NeoPixelI2S(uint16_t count, int pin)
        : m_count(count),
          m_brightness(255),
          m_strip(count, (uint8_t)pin),
          m_buf(count, RgbColor(0)) {}

    void begin() {
        m_strip.Begin();
        m_strip.Show();  // blank the strip
    }

    // Adafruit-compatible packed colour helpers (0x00RRGGBB / 0xWWRRGGBB).
    static uint32_t Color(uint8_t r, uint8_t g, uint8_t b) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    static uint32_t Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
        return ((uint32_t)w << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    void setBrightness(uint8_t b) { m_brightness = b; }

    void setPixelColor(uint16_t i, uint32_t c) {
        if (i < m_count) m_buf[i] = unpack(c);
    }

    void fill(uint32_t c = 0, uint16_t first = 0, uint16_t count = 0) {
        if (first >= m_count) return;
        uint32_t last = (count == 0) ? m_count : (uint32_t)first + count;
        if (last > m_count) last = m_count;
        RgbColor rc = unpack(c);
        for (uint16_t i = first; i < last; i++) m_buf[i] = rc;
    }

    void clear() {
        for (uint16_t i = 0; i < m_count; i++) m_buf[i] = RgbColor(0);
    }

    void show() {
        for (uint16_t i = 0; i < m_count; i++) {
            RgbColor c = m_buf[i];
            if (m_brightness != 255) {
                c.R = (uint8_t)((uint16_t)c.R * m_brightness / 255);
                c.G = (uint8_t)((uint16_t)c.G * m_brightness / 255);
                c.B = (uint8_t)((uint16_t)c.B * m_brightness / 255);
            }
            m_strip.SetPixelColor(i, c);
        }
        m_strip.Show();
    }

   private:
    static RgbColor unpack(uint32_t c) {
        return RgbColor((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }

    uint16_t m_count;
    uint8_t m_brightness;
    NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1Ws2812xMethod> m_strip;
    std::vector<RgbColor> m_buf;
};
