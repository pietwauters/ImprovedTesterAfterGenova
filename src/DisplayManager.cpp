#include "DisplayManager.h"

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>

#include <vector>

#include "globals.h"

// Table: Unicode codepoint to CP437 byte (0-255). Unmapped = 0xFF.
struct UnicodeToCP437 {
    uint16_t unicode;
    uint8_t cp437;
};

// Table covers all CP437 special chars (including Spanish, German, French, etc.)
static const UnicodeToCP437 unicodeToCp437Table[] = {
    {0x00A1, 173},  // ¡
    {0x00A2, 155},  // ¢
    {0x00A3, 156},  // £
    {0x00A5, 157},  // ¥
    {0x00A7, 21},   // §
    {0x00AA, 166},  // ª
    {0x00AB, 174},  // «
    {0x00AC, 170},  // ¬
    {0x00B0, 248},  // °
    {0x00B1, 241},  // ±
    {0x00B2, 253},  // ²
    {0x00B5, 230},  // µ
    {0x00B7, 250},  // ·
    {0x00BA, 167},  // º
    {0x00BB, 175},  // »
    {0x00BC, 172},  // ¼
    {0x00BD, 171},  // ½
    {0x00BF, 168},  // ¿
    {0x00C0, 183},  // À
    {0x00C1, 181},  // Á
    {0x00C2, 182},  // Â
    {0x00C3, 199},  // Ã
    {0x00C4, 142},  // Ä
    {0x00C5, 143},  // Å
    {0x00C6, 146},  // Æ
    {0x00C7, 128},  // Ç
    {0x00C8, 212},  // È
    {0x00C9, 144},  // É
    {0x00CA, 210},  // Ê
    {0x00CB, 211},  // Ë
    {0x00CC, 222},  // Ì
    {0x00CD, 214},  // Í
    {0x00CE, 215},  // Î
    {0x00CF, 216},  // Ï
    {0x00D1, 165},  // Ñ
    {0x00D2, 227},  // Ò
    {0x00D3, 224},  // Ó
    {0x00D4, 226},  // Ô
    {0x00D5, 229},  // Õ
    {0x00D6, 153},  // Ö
    {0x00D7, 158},  // ×
    {0x00D8, 157},  // Ø
    {0x00D9, 235},  // Ù
    {0x00DA, 233},  // Ú
    {0x00DB, 234},  // Û
    {0x00DC, 154},  // Ü
    {0x00DF, 225},  // ß
    {0x00E0, 133},  // à
    {0x00E1, 160},  // á
    {0x00E2, 131},  // â
    {0x00E3, 198},  // ã
    {0x00E4, 132},  // ä
    {0x00E5, 134},  // å
    {0x00E6, 145},  // æ
    {0x00E7, 135},  // ç
    {0x00E8, 138},  // è
    {0x00E9, 130},  // é
    {0x00EA, 136},  // ê
    {0x00EB, 137},  // ë
    {0x00EC, 141},  // ì
    {0x00ED, 161},  // í
    {0x00EE, 140},  // î
    {0x00EF, 139},  // ï
    {0x00F1, 164},  // ñ
    {0x00F2, 149},  // ò
    {0x00F3, 162},  // ó
    {0x00F4, 147},  // ô
    {0x00F5, 228},  // õ
    {0x00F6, 148},  // ö
    {0x00F7, 246},  // ÷
    {0x00F8, 155},  // ø
    {0x00F9, 151},  // ù
    {0x00FA, 163},  // ú
    {0x00FB, 150},  // û
    {0x00FC, 129},  // ü
    {0x00FF, 152},  // ÿ
    // Add more as needed for full CP437 coverage
};

static uint8_t utf8CodepointToCp437(uint16_t codepoint) {
    if (codepoint < 128)
        return (uint8_t)codepoint;  // ASCII
    // Search table
    for (size_t i = 0; i < sizeof(unicodeToCp437Table) / sizeof(unicodeToCp437Table[0]); ++i) {
        if (unicodeToCp437Table[i].unicode == codepoint)
            return unicodeToCp437Table[i].cp437;
    }
    return '?';  // fallback for unsupported
}

String DisplayManager::utf8ToCp437(const String& utf8str) {
    std::vector<uint8_t> out;
    const char* s = utf8str.c_str();
    while (*s) {
        uint8_t c = (uint8_t)*s;
        if (c < 0x80) {
            out.push_back(c);
            s++;
        } else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8
            uint16_t codepoint = ((c & 0x1F) << 6) | (s[1] & 0x3F);
            out.push_back(utf8CodepointToCp437(codepoint));
            s += 2;
        } else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 (rare for CP437)
            uint16_t codepoint = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            out.push_back(utf8CodepointToCp437(codepoint));
            s += 3;
        } else {
            // Invalid/unsupported
            out.push_back('?');
            s++;
        }
    }
    // Build String from bytes
    String result;
    for (uint8_t b : out) result += (char)b;
    return result;
}

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
