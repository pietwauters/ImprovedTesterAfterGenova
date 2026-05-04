#include "DisplayManager.h"

#include <Wire.h>

#include "globals.h"

// Convenience reference to the singleton
DisplayManager& Display = DisplayManager::instance();

void DisplayManager::begin() {
    if (initialized)
        return;
    initialized = true;
    Wire.begin(SDA_PIN, SCL_PIN);
    Adafruit_SSD1306::begin(SSD1306_SWITCHCAPVCC, 0x3C);
    setRotation(2);
    clearDisplay();
    setTextSize(1);
    setCursor(0, 0);
    setTextColor(SSD1306_WHITE);
    cp437(true);
    display();
    setMode("");
    // showMode();
}

constexpr int R_X = 0;
constexpr int R_Y = 15;
constexpr int R_X_Subscript = R_X + 12;
constexpr int R_Y_Subscript = R_Y + 10;

void DisplayManager::initForSingleValue(const char* label) {
    clearDisplay();
    showMode();
    setTextSize(2);
    setCursor(R_X, R_Y);
    printf("R");
    setTextSize(1);
    setCursor(R_X_Subscript, R_Y_Subscript);
    printf(label);

    setTextSize(2);
    CursX = getCursorX();
    setCursor(CursX, R_Y);

    printf(" = ");
    CursX = getCursorX();
    display();
    // calculate clearing rectangle
    const char* mask = "50.99 ";
    getTextBounds(mask, CursX, R_Y, &x1, &y1, &w, &h);
}
void DisplayManager::showMode() {
    clearDisplay();
    setTextSize(1);
    setCursor(0, 0);
    printf(mode);
    printf(":");
    display();
}

void DisplayManager::showSingleValue(float r) {
    setTextSize(2);
    setCursor(CursX, 14);
    if (r < 0.0) {
        r = 0.0;
    }
    if (abs(lastvalue - r) < 0.1f)
        return;
    if (lastvalue < 10.1f) {
        lastvalue = (9.0 * lastvalue + r) / 10.0f;
    } else {  // For large values, no averaging
        lastvalue = r;
    }

    fillRect(CursX, 14, w, h, BLACK);

    if (r < 99.0f) {
        if (r < 0.0f)
            r = 0.0f;
        printf("%.1f", lastvalue);
    } else {
        if (r < 999.0f) {
            printf("%.0f", lastvalue);
        } else
            printf("----");
    }
    display();
}

void DisplayManager::clear() {
    clearDisplay();
    display();
}

void DisplayManager::printUnderlined(const char* text) {
    int16_t tx1, ty1;
    uint16_t tw, th;
    int16_t cx = getCursorX();
    int16_t cy = getCursorY();
    getTextBounds(text, cx, cy, &tx1, &ty1, &tw, &th);
    print(text);
    if (tx1 > 1) {
        drawFastHLine(tx1 - 2, ty1 + th + 1, tw + 2, SSD1306_WHITE);
    } else {
        drawFastHLine(tx1, ty1 + th + 1, tw, SSD1306_WHITE);
    }
}

void DisplayManager::showWiretesting1() {
    clearDisplay();
    setTextSize(1);
    setCursor(2, 0);
    printUnderlined("A-A'");
    printf("    ");
    printUnderlined("B-B'");
    printf("    ");
    printUnderlined("C-C'");

    display();
}

String DisplayManager::formatResistance(float r) {
    if (r < 0.0f)
        r = 0.0f;
    if (r >= OPEN_RESISTANCE)
        return "---";
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f", r);
    return String(buf);
}

void DisplayManager::showWiretesting1Values(float r1, float r2, float r3) {
    fillRect(0, 18, 128, 32 - 18, BLACK);
    setCursor(2, 18);
    printf("%-5s   %-5s   %-5s", formatResistance(r1).c_str(), formatResistance(r2).c_str(),
           formatResistance(r3).c_str());
    display();
}
