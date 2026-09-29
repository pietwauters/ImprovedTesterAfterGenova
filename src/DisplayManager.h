#pragma once

#include <Adafruit_SSD1306.h>
#include <Arduino.h>

#include "Hardware.h"  // HARDWARE_REV guard + revision-specific pins
// Define the OLED display dimensions
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32  // Adjust for your display's size
// Define custom I2C pins (revision specific)
#if HARDWARE_REV == 1
#define SDA_PIN 17
#define SCL_PIN 16
#elif HARDWARE_REV == 2
// TODO(hw_rev2): update the OLED I2C pins for the new board. Currently an exact
// copy of hw_rev1 — change only if they actually differ on the new hardware.
#define SDA_PIN 17  // unchanged
#define SCL_PIN 4   // was 16
#elif HARDWARE_REV == 3
// TODO(hw_rev3): OLED I2C pins, copied from hw_rev2.
#define SDA_PIN 17
#define SCL_PIN 4
#endif

class DisplayManager : public Adafruit_SSD1306 {
   public:
    // Resistance above this threshold is displayed as "---"
    static constexpr float OPEN_RESISTANCE = 999.99f;

    // Singleton access
    static DisplayManager& instance() {
        static DisplayManager inst;
        return inst;
    }

    // Delete copy/move so the singleton can't be duplicated
    DisplayManager(const DisplayManager&) = delete;
    DisplayManager& operator=(const DisplayManager&) = delete;

    void begin();
    void initForSingleValue(const char* label);
    void showSingleValue(float r);
    void showMode();
    void showWiretesting1();
    void showWiretesting1Values(float r1, float r2, float r3);
    void clear();
    void setMode(const char* _mode) { strncpy(mode, _mode, 15); }
    // Prints text at the current cursor position and draws a 1-px underline below it
    void printUnderlined(const char* text);
    // Returns a formatted resistance string: "---" if r >= OPEN_RESISTANCE, else "x.x"
    String formatResistance(float r);

    // Converts a UTF-8 string to CP437 encoding for OLED display
    static String utf8ToCp437(const String& utf8str);

   private:
    DisplayManager() : Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {}

    bool initialized = false;
    // Internal cursor and smoothing state
    int CursX = 0;
    float lastvalue = 0.0f;
    int16_t x1, y1;
    uint16_t w, h;
    char mode[16];
};

// Convenience reference — same as DisplayManager::instance()
extern DisplayManager& Display;
