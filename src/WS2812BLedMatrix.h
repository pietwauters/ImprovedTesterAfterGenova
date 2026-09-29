// Copyright (c) Piet Wauters 2022 <piet.wauters@gmail.com>
#pragma once
#include "Hardware.h"  // HARDWARE_REV guard + revision-specific pins
#include "NeoPixelRMT.h"
// #include "SubjectObserverTemplate.h"

// =============================================================================
// LED panel layout  (full explanation: docs/LED_PANEL_LAYOUT.md)
// =============================================================================
// All drawing code works in USER VIEW: a 5x5 grid exactly as the fencer sees
// it in normal use, addressed row-major, u = row*5 + col, 0 = top-left.
// Glyphs are 5-row bitmaps (bit 4 = leftmost column).
//
// How the physical LED chain snakes through that grid is described by ONE
// layout string, "<corner>-<direction>[-P]":
//   corner    = where LED #0 (first LED after the data input) sits, as seen by
//               the user: BL, BR, TL, TR
//   direction = which way the chain runs from there: V (vertical, first run is
//               a column) or H (horizontal, first run is a row)
//   -P        = optional: progressive wiring (every run in the same direction)
//               instead of serpentine/snake (default)
// e.g. "BL-V" = LED 0 bottom-left, up the first column, down the second, ...
// These 8 snake layouts cover every panel rotation and mirroring (viewed from
// the back). The layout is a runtime setting (LedLayout); the default below is
// per board. Verify with the terminal command `ledtest` (draws an F, which
// looks different in all 8 layouts).
//
// Separately, LED_WIRE0_ON_LEFT (per board, compile time) says on which side
// the wire drawings start: wire k is column 4-2k (false) or 2k (true). This
// follows where the banana jacks physically sit relative to the display, so it
// only mirrors wire content (lines, connection/short/swap animations), never
// glyphs.
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////////
// Which pin on the Arduino is connected to the NeoPixels?

#if HARDWARE_REV == 1
constexpr int PIN = 26;
constexpr const char* LED_LAYOUT_DEFAULT = "TL-V";  // inferred from legacy tables, not verified - run ledtest
constexpr bool LED_WIRE0_ON_LEFT = false;
#elif HARDWARE_REV == 2
constexpr int PIN = 16;
constexpr const char* LED_LAYOUT_DEFAULT = "TL-V";  // derived from the verified rev2 R/E/F glyph maps
constexpr bool LED_WIRE0_ON_LEFT = false;
#elif HARDWARE_REV == 3
constexpr int PIN = 16;
constexpr const char* LED_LAYOUT_DEFAULT = "BR-V";  // native D5-D29 on the board bottom (verified with glyphs)
constexpr bool LED_WIRE0_ON_LEFT = true;  // jack order is mirrored vs rev1/2 (verified on the bench)
#endif
constexpr int BUZZERPIN = 22;
constexpr int RELATIVE_HIGH = HIGH;
constexpr int RELATIVE_LOW = LOW;

// How many NeoPixels are attached to the Arduino?
constexpr int LED_GRID = 5;  // panel is LED_GRID x LED_GRID
constexpr int NUMPIXELS = LED_GRID * LED_GRID;

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.

/////////////////////////////////////////////////////////////////////////////////////

constexpr uint8_t MASK_RED = 0x80;
constexpr uint8_t MASK_WHITE_L = 0x40;
constexpr uint8_t MASK_ORANGE_L = 0x20;
constexpr uint8_t MASK_ORANGE_R = 0x10;
constexpr uint8_t MASK_WHITE_R = 0x08;
constexpr uint8_t MASK_GREEN = 0x04;
constexpr uint8_t MASK_BUZZ = 0x02;

constexpr uint8_t BRIGHTNESS_LOW = 10;
constexpr uint8_t BRIGHTNESS_NORMAL = 25;
constexpr uint8_t BRIGHTNESS_HIGH = 60;
constexpr uint8_t BRIGHTNESS_ULTRAHIGH = 100;

class WS2812B_LedMatrix {
   public:
    /** Default constructor */
    WS2812B_LedMatrix();
    /** Default destructor */
    virtual ~WS2812B_LedMatrix();

    /** Access m_LedStatus
     * \return The current value of m_LedStatus
     */

    /** Set m_LedStatus
     * \param val New value to set
     */
    // Select the panel layout ("BL-V", "BR-H", "TL-V-P", ...; case-insensitive).
    // Returns false and keeps the current layout if the string is invalid.
    bool setLayout(const String& spec);
    String getLayout() const { return m_layoutName; }
    static bool isValidLayout(const String& spec);
    // Layout self-test: draws an upright F (user view) plus chain LED #0 red and
    // #1 orange. If the F is not upright and unmirrored, the layout is wrong.
    void LayoutTest();
    void ClearAll();
    void setBuzz(bool Value);
    void myShow() { m_pixels->show(); };
    void SetBrightness(uint8_t val);
    void begin();
    void SetLine(int i, uint32_t theColor);
    void SetFullMatrix(uint32_t theColor) {
        m_pixels->fill(theColor, 0, NUMPIXELS);
        myShow();
    };
    void SetInner9(uint32_t theColor);
    void SetSwappedLines(int i, int j);
    void AnimateSwap(int i, int j);
    void AnimateShort(int i, int j);
    void AnimateGoodConnection(int k, int level = 0);
    void AnimateBrokenConnection(int k);
    void AnimateWrongConnection(int i, int j);
    void AnimateArBrConnection();
    void AnimateBrCrConnection();
    void DrawDiamond(uint32_t theColor);
    void Draw_E(uint32_t theColor);
    void Draw_F(uint32_t theColor);
    void Draw_P(uint32_t theColor);
    void Draw_C(uint32_t theColor);
    void Draw_R(uint32_t theColor);
    void Draw_GND(uint32_t theColor);
    void Draw_SinglePixel(int Pixel, uint32_t theColor) { setUserPixel(Pixel, theColor); };  // user-view index
    void SequenceTest();
    void ConfigureBlinking(int PixelNr, uint32_t theColor, int OnTime = 100, int OffTime = 100, int Repeat = 0);
    void Blink();
    void RestartBlink();
    void SetBlinkColor(uint32_t theColor) { m_BlinkingColor = theColor; };
    bool GetBlinkState() { return m_BlinkingState; };

    uint32_t m_Red;
    uint32_t m_Purple;
    uint32_t m_Green;
    uint32_t m_White;
    uint32_t m_Orange;
    uint32_t m_Yellow;
    uint32_t m_Blue;
    uint32_t m_Off;

   protected:
   private:
    // User-view index (row*5 + col, 0 = top-left) -> physical LED-chain index.
    // Built by setLayout(); every pixel write goes through it.
    uint8_t m_map[NUMPIXELS];
    String m_layoutName;
    static int XY(int row, int col) { return row * LED_GRID + col; }
    void setUserPixel(int u, uint32_t theColor) { m_pixels->setPixelColor(m_map[u], theColor); }
    // Wire content is authored with wire 0 on the right; mirrored when the
    // board's jack order puts wire 0 on the left.
    static int wireU(int u) { return LED_WIRE0_ON_LEFT ? XY(u / LED_GRID, LED_GRID - 1 - u % LED_GRID) : u; }
    void setWirePixel(int u, uint32_t theColor) { setUserPixel(wireU(u), theColor); }
    void drawBitmap(const uint8_t rows[LED_GRID], uint32_t theColor);

    NeoPixelRMT* m_pixels;
    uint8_t m_Brightness = BRIGHTNESS_LOW;
    bool m_Loudness = true;
    int animationspeed = 60;
    QueueHandle_t queue = NULL;
    int m_BlinkingPixel = -1;  // -1 means no blinking
    uint32_t m_BlinkingColor = 0;
    int m_BlinkingOnTime = 100;
    int m_BlinkingOffTime = 100;
    int m_BlinkingRepeat = 0;  // 0 means infinite blinking
    bool m_BlinkingState = false;
    long m_BlinkingNextTimeToChange = 0;
};
