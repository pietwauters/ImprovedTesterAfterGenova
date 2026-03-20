#include "DisplayManager.h"

#include <Wire.h>

#include "globals.h"

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void DisplayManager::begin() {
    Wire.begin(SDA_PIN, SCL_PIN);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.setRotation(2);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 12);
    display.print(F("Tester v1.1.14\n"));
    display.cp437(true);
    display.setCursor(0, 12);
    // display.printf("Resistance: %.2f ", 0.34);
    // display.write(0xEA);
    display.println();
    display.display();
    setMode("Waiting");
    showMode();
}

constexpr int R_X = 0;
constexpr int R_Y = 15;
constexpr int R_X_Subscript = R_X + 12;
constexpr int R_Y_Subscript = R_Y + 10;
void DisplayManager::initForSingleValue(const char* label) {
    display.clearDisplay();
    showMode();
    display.setTextSize(2);
    display.setCursor(R_X, R_Y);
    display.printf("R");
    display.setTextSize(1);
    display.setCursor(R_X_Subscript, R_Y_Subscript);
    display.printf(label);

    display.setTextSize(2);
    CursX = display.getCursorX();
    display.setCursor(CursX, R_Y);
    display.printf(" = ");
    CursX = display.getCursorX();
    display.display();
    // calculate clearing rectangle
    const char* mask = "50.99 ";
    display.getTextBounds(mask, CursX, R_Y, &x1, &y1, &w, &h);
}
void DisplayManager::showMode() {
    display.setTextSize(1);
    // display.setCursor(R_X_Subscript + 4, 0);
    display.setCursor(0, 0);
    display.printf(mode);
    display.display();
}

void DisplayManager::showSingleValue(float r) {
    display.setTextSize(2);
    display.setCursor(CursX, 14);
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

    display.fillRect(CursX, 14, w, h, BLACK);

    if (r < 50.0f) {
        if (r < 0.0f)
            r = 0.0f;
        display.printf("%.1f", lastvalue);
    } else {
        display.printf("----");
    }
    display.display();
}

void DisplayManager::clear() {
    display.clearDisplay();
    display.display();
}

DisplayManager Display;
