#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
// Define the OLED display dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32  // Adjust for your display's size
// Define custom I2C pins
#define SDA_PIN 17
#define SCL_PIN 16

class DisplayManager {
   public:
    void begin();
    void initForSingleValue(const char* label);
    void showSingleValue(float r);
    void showMode();
    void clear();
    void setMode(const char* _mode) { strncpy(mode, _mode, 15); }

   private:
    // Internal cursor and smoothing state
    int CursX = 0;
    float lastvalue = 0.0f;
    int16_t x1, y1;
    uint16_t w, h;
    char mode[16];
};

extern DisplayManager Display;
