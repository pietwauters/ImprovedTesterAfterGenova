// Copyright (c) Piet Wauters 2022 <piet.wauters@gmail.com>
#include "WS2812BLedMatrix.h"
// All pixel indices in this file are USER VIEW (u = row*5 + col, 0 = top-left
// as the fencer sees the panel). setLayout() builds m_map[] which translates
// them to the physical LED chain. See WS2812BLedMatrix.h and
// docs/LED_PANEL_LAYOUT.md.
//
// User-view index grid:
//  0  1  2  3  4
//  5  6  7  8  9
// 10 11 12 13 14
// 15 16 17 18 19
// 20 21 22 23 24
//
// The three wires are drawn as columns: wire k (0..2) is column 4 - 2k, or
// column 2k on boards with LED_WIRE0_ON_LEFT (see setWirePixel()).

bool WS2812B_LedMatrix::isValidLayout(const String& spec) {
    String s = spec;
    s.trim();
    s.toUpperCase();
    if (s.length() != 4 && s.length() != 6)
        return false;
    if ((s[0] != 'B' && s[0] != 'T') || (s[1] != 'L' && s[1] != 'R') || s[2] != '-' || (s[3] != 'V' && s[3] != 'H'))
        return false;
    return s.length() == 4 || s.substring(4) == "-P";
}

bool WS2812B_LedMatrix::setLayout(const String& spec) {
    if (!isValidLayout(spec))
        return false;
    String s = spec;
    s.trim();
    s.toUpperCase();
    const bool top = s[0] == 'T';
    const bool left = s[1] == 'L';
    const bool vertical = s[3] == 'V';
    const bool serpentine = s.length() == 4;

    for (int k = 0; k < NUMPIXELS; k++) {
        // Chain LED k is the pos-th LED of run 'run', counted from the start corner.
        int run = k / LED_GRID;
        int pos = k % LED_GRID;
        if (serpentine && (run & 1))
            pos = LED_GRID - 1 - pos;  // odd runs come back the other way
        // Distance from the start corner, per axis.
        int dCol = vertical ? run : pos;
        int dRow = vertical ? pos : run;
        int col = left ? dCol : LED_GRID - 1 - dCol;
        int row = top ? dRow : LED_GRID - 1 - dRow;
        m_map[XY(row, col)] = k;
    }
    m_layoutName = s;
    return true;
}

void WS2812B_LedMatrix::drawBitmap(const uint8_t rows[LED_GRID], uint32_t theColor) {
    for (int r = 0; r < LED_GRID; r++) {
        for (int c = 0; c < LED_GRID; c++) {
            if (rows[r] & (1 << (LED_GRID - 1 - c)))
                setUserPixel(XY(r, c), theColor);
        }
    }
}

WS2812B_LedMatrix::WS2812B_LedMatrix() {
    // ctor
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);
    pinMode(BUZZERPIN, OUTPUT);
    digitalWrite(BUZZERPIN, RELATIVE_LOW);
    m_pixels = new NeoPixelRMT(NUMPIXELS, (gpio_num_t)PIN);
    SetBrightness(BRIGHTNESS_NORMAL);
    setLayout(LED_LAYOUT_DEFAULT);

    // queue = xQueueCreate( 60, sizeof( int ) );
}
void WS2812B_LedMatrix::begin() {
    pinMode(PIN, OUTPUT);
    digitalWrite(PIN, LOW);
    m_pixels->begin();
    m_pixels->fill(m_pixels->Color(0, 0, 0), 0, NUMPIXELS);
    m_pixels->clear();
    m_pixels->show();
}

void WS2812B_LedMatrix::SetBrightness(uint8_t val) {
    m_Brightness = val;
    m_pixels->setBrightness(m_Brightness);
    m_Red = NeoPixelRMT::Color(255, 0, 0, m_Brightness);
    m_Green = NeoPixelRMT::Color(0, 255, 0, m_Brightness);
    m_White = NeoPixelRMT::Color(200, 200, 200, m_Brightness);
    m_Orange = NeoPixelRMT::Color(255, 70, 0, m_Brightness);
    m_Yellow = NeoPixelRMT::Color(255, 251, 0, m_Brightness);
    m_Blue = NeoPixelRMT::Color(0, 0, 255, m_Brightness);
    m_Purple = NeoPixelRMT::Color(105, 0, 200, m_Brightness);
    m_Off = NeoPixelRMT::Color(0, 0, 0, m_Brightness);
}

WS2812B_LedMatrix::~WS2812B_LedMatrix() {
    // dtor
    delete m_pixels;
}

void WS2812B_LedMatrix::ClearAll() {
    setBuzz(false);
    m_pixels->fill(m_pixels->Color(0, 0, 0), 0, NUMPIXELS);
    myShow();
}

void WS2812B_LedMatrix::LayoutTest() {
    // Upright F in user view: only correct if the layout matches the panel.
    static const uint8_t testF[LED_GRID] = {
        0b11111,  // #####
        0b10000,  // #....
        0b11110,  // ####.
        0b10000,  // #....
        0b10000,  // #....
    };
    ClearAll();
    drawBitmap(testF, m_Green);
    // Also mark the start of the chain, in PHYSICAL chain order (bypasses the
    // map on purpose): LED #0 red, LED #1 orange. This shows the start corner
    // and the first direction directly, whatever the layout setting says.
    m_pixels->setPixelColor(0, m_Red);
    m_pixels->setPixelColor(1, m_Orange);
    m_pixels->show();
}

// Welcome animation: a column snake starting top-right.
static const uint8_t welcome_sequence[NUMPIXELS] = {4,  9,  14, 19, 24, 23, 18, 13, 8, 3, 2,  7,  12,
                                                    17, 22, 21, 16, 11, 6,  1,  0,  5, 10, 15, 20};

void WS2812B_LedMatrix::SequenceTest() {
    ClearAll();
    m_pixels->show();
    for (int i = 0; i < NUMPIXELS; i++) {
        setUserPixel(welcome_sequence[i], (i % 2) ? m_Yellow : m_Orange);
        m_pixels->show();
        delay(animationspeed);
    }
    ClearAll();
    m_pixels->show();
}

void WS2812B_LedMatrix::setBuzz(bool Value) {
    if (m_Loudness) {
        if (Value) {
            digitalWrite(BUZZERPIN, RELATIVE_HIGH);
        } else {
            digitalWrite(BUZZERPIN, RELATIVE_LOW);
        }
    }
}

// Everything from here to the glyphs is WIRE content, authored with wire 0 on
// the right (wire i = column 4 - 2i) and drawn via setWirePixel(), which mirrors
// it when LED_WIRE0_ON_LEFT. SetLine(i) lights column 4 - i.
void WS2812B_LedMatrix::SetLine(int i, uint32_t theColor) {
    for (int r = 0; r < LED_GRID; r++) {
        setWirePixel(XY(r, LED_GRID - 1 - i), theColor);
    }
}

// Two crossing paths per wire pair: [pair][path][step]. Pair 0 = wires 0-1,
// 1 = wires 1-2, 2 = wires 0-2.
static const uint8_t animation_sequence[][2][7] = {
    {{4, 9, 14, 13, 12, 17, 22}, {2, 7, 12, 13, 14, 19, 24}},
    {{2, 7, 12, 11, 10, 15, 20}, {0, 5, 10, 11, 12, 17, 22}},
    {{4, 9, 8, 12, 16, 15, 20}, {0, 5, 6, 12, 18, 19, 24}},
};

void WS2812B_LedMatrix::AnimateSwap(int i, int j) {
    int k, l, m;
    if (i > j) {
        k = j;
        l = i;
    } else {
        k = i;
        l = j;
    }

    uint32_t currentcolor = m_Blue;
    if (l - k == 1) {
        m = k;
    } else {
        m = 2;
    }
    delay(100);
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 7; i++) {
            setWirePixel(animation_sequence[m][j][i], currentcolor);
            m_pixels->show();
            delay(70 * animationspeed);
        }
        currentcolor = m_Red;
        delay(300);
    }
    m_pixels->show();
}

// We arrange the sequences such that m = 2*(i+1)-j;
static const uint8_t animation_sequence_wrong[][7] = {
    {24, 24, 18, 12, 6, 0, 0},     // 0-2
    {24, 19, 14, 13, 12, 7, 2},    // 0-1
    {22, 17, 12, 11, 10, 5, 0},    // 1-2
    {24, 19, 14, 9, 4, 4, 4},      // unused
    {22, 17, 12, 13, 14, 9, 4},    // 1-0
    {20, 15, 10, 11, 12, 7, 2},    // 2-1
    {20, 20, 16, 12, 8, 4, 4}      // 2-0
};

void WS2812B_LedMatrix::AnimateWrongConnection(int i, int j) {
    uint32_t currentcolor = m_Blue;
    int m = (i + 1) * 2 - j;

    for (int i = 0; i < 7; i++) {
        setWirePixel(animation_sequence_wrong[m][i], currentcolor);
        m_pixels->show();
        delay(70 * animationspeed / 100);
    }
}

void WS2812B_LedMatrix::AnimateShort(int i, int j) {
    int k, l, m;
    if (i > j) {
        k = j;
        l = i;
    } else {
        k = i;
        l = j;
    }

    uint32_t currentcolor = m_Yellow;
    if (l - k == 1) {
        m = k;
    } else {
        m = 2;
    }
    delay(100);
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 7; i++) {
            setWirePixel(animation_sequence[m][j][i], currentcolor);
            m_pixels->show();
            delay(70 * animationspeed / 100);
        }

        delay(200 * animationspeed / 100);
    }
    m_pixels->show();
}

// Wire k (column 4 - 2k), drawn bottom to top.
void WS2812B_LedMatrix::AnimateGoodConnection(int k, int level) {
    uint32_t currentcolor = m_Green;
    switch (level) {
        case 1:
            currentcolor = m_Yellow;
            break;

        case 2:
            currentcolor = m_Orange;
            break;
    }
    for (int r = LED_GRID - 1; r >= 0; r--) {
        setWirePixel(XY(r, 4 - 2 * k), currentcolor);
        m_pixels->show();
        delay(60 * animationspeed / 100);
    }
}

// Wire k (column 4 - 2k): every other pixel, bottom to top.
void WS2812B_LedMatrix::AnimateBrokenConnection(int k) {
    for (int r = 4; r >= 0; r -= 2) {
        setWirePixel(XY(r, 4 - 2 * k), m_Red);
        m_pixels->show();
        delay(140 * animationspeed / 100);
    }
}

// [pair k][0..4 blue, 5..9 purple] for adjacent wires k and k+1.
static const uint8_t swapped_adjacent[2][10] = {
    {4, 9, 14, 23, 18, 24, 3, 13, 19, 8},
    {23, 18, 13, 2, 7, 3, 22, 12, 8, 17},
};
static const uint8_t swapped_far_blue[] = {4, 9, 17, 22, 13};
static const uint8_t swapped_far_purple[] = {24, 18, 2, 7, 12};

void WS2812B_LedMatrix::SetSwappedLines(int i, int j) {
    int k, l;
    if (i > j) {
        k = j;
        l = i;
    } else {
        k = i;
        l = j;
    }
    if ((l - k == 1)) {
        for (int n = 0; n < 10; n++) {
            setWirePixel(swapped_adjacent[k][n], n < 5 ? m_Blue : m_Purple);
        }
    } else {
        for (uint8_t u : swapped_far_blue) setWirePixel(u, m_Blue);
        for (uint8_t u : swapped_far_purple) setWirePixel(u, m_Purple);
    }
}

static const uint8_t animation_sequence_ArBr[] = {22, 17, 16, 15, 20};

void WS2812B_LedMatrix::AnimateArBrConnection() {
    uint32_t currentcolor = m_Blue;

    for (uint8_t u : animation_sequence_ArBr) {
        setWirePixel(u, currentcolor);
        m_pixels->show();
        delay(170 * animationspeed / 100);
    }
    delay(300 * animationspeed / 100);
    ClearAll();
}

static const uint8_t animation_sequence_BrCr[] = {24, 19, 18, 17, 16, 15, 20};

void WS2812B_LedMatrix::AnimateBrCrConnection() {
    uint32_t currentcolor = m_Blue;

    for (uint8_t u : animation_sequence_BrCr) {
        setWirePixel(u, currentcolor);
        m_pixels->show();
        delay(130 * animationspeed / 100);
    }
    delay(300 * animationspeed / 100);
    ClearAll();
}

// Glyphs: one byte per row, top row first, bit 4 = leftmost column (user view).
static const uint8_t Letter_P[LED_GRID] = {
    0b00100,  // ..#..
    0b00100,  // ..#..
    0b11111,  // #####
    0b00100,  // ..#..
    0b00100,  // ..#..
};
void WS2812B_LedMatrix::Draw_P(uint32_t theColor) {
    drawBitmap(Letter_P, theColor);
    m_pixels->show();
}

static const uint8_t Letter_C[LED_GRID] = {
    0b10111,  // #.###
    0b00001,  // ....#
    0b10001,  // #...#
    0b10001,  // #...#
    0b10111,  // #.###
};
void WS2812B_LedMatrix::Draw_C(uint32_t theColor) {
    drawBitmap(Letter_C, theColor);
    m_pixels->show();
}

static const uint8_t Letter_R[LED_GRID] = {
    0b11100,  // ###..
    0b10010,  // #..#.
    0b11100,  // ###..
    0b10010,  // #..#.
    0b10010,  // #..#.
};
void WS2812B_LedMatrix::Draw_R(uint32_t theColor) {
    drawBitmap(Letter_R, theColor);
    m_pixels->show();
}

static const uint8_t DiamondShape[LED_GRID] = {
    0b00100,  // ..#..
    0b01010,  // .#.#.
    0b10001,  // #...#
    0b01010,  // .#.#.
    0b00100,  // ..#..
};
void WS2812B_LedMatrix::DrawDiamond(uint32_t theColor) {
    drawBitmap(DiamondShape, theColor);
    m_pixels->show();
}

static const uint8_t Letter_E[LED_GRID] = {
    0b11110,  // ####.
    0b10000,  // #....
    0b11100,  // ###..
    0b10000,  // #....
    0b11110,  // ####.
};
void WS2812B_LedMatrix::Draw_E(uint32_t theColor) {
    drawBitmap(Letter_E, theColor);
    m_pixels->show();
}

static const uint8_t Letter_F[LED_GRID] = {
    0b01110,  // .###.
    0b01000,  // .#...
    0b01100,  // .##..
    0b01000,  // .#...
    0b01000,  // .#...
};
void WS2812B_LedMatrix::Draw_F(uint32_t theColor) {
    drawBitmap(Letter_F, theColor);
    m_pixels->show();
}

// Ground symbol. (The pre-layout table for the default build was a copy of the
// F table, so this used to draw an F.)
static const uint8_t Symbol_GND[LED_GRID] = {
    0b00100,  // ..#..
    0b00100,  // ..#..
    0b11111,  // #####
    0b01110,  // .###.
    0b00100,  // ..#..
};
void WS2812B_LedMatrix::Draw_GND(uint32_t theColor) {
    drawBitmap(Symbol_GND, theColor);
    m_pixels->show();
}

void WS2812B_LedMatrix::ConfigureBlinking(int PixelNr, uint32_t theColor, int OnTime, int OffTime, int Repeat) {
    m_BlinkingPixel = PixelNr;  // -1 means no blinking
    m_BlinkingColor = theColor;
    m_BlinkingOnTime = OnTime;
    m_BlinkingOffTime = OffTime;
    m_BlinkingRepeat = Repeat;  // 0 means infinite blinking
    m_BlinkingState = false;
}

void WS2812B_LedMatrix::Blink() {
    if (m_BlinkingPixel < 0)
        return;  // No blinking configured
    long currentTime = millis();
    if (m_BlinkingNextTimeToChange > currentTime)
        return;

    if (m_BlinkingState) {
        // Turn off the blinking pixel
        setUserPixel(m_BlinkingPixel, m_Off);
        m_BlinkingState = false;
        m_BlinkingNextTimeToChange = currentTime + m_BlinkingOffTime;
    } else {
        // Turn on the blinking pixel
        setUserPixel(m_BlinkingPixel, m_BlinkingColor);
        m_BlinkingNextTimeToChange = currentTime + m_BlinkingOnTime;
        m_BlinkingState = true;
    }
    m_pixels->show();
}

void WS2812B_LedMatrix::RestartBlink() {
    m_BlinkingState = true;
    m_BlinkingNextTimeToChange = millis() + m_BlinkingOnTime;  // Start with off state
    if (m_BlinkingPixel >= 0) {
        setUserPixel(m_BlinkingPixel, m_BlinkingColor);  // Ensure pixel is off initially
        m_pixels->show();
    }
}

void WS2812B_LedMatrix::SetInner9(uint32_t theColor) {
    ClearAll();
    for (int r = 1; r <= 3; r++) {
        for (int c = 1; c <= 3; c++) setUserPixel(XY(r, c), theColor);
    }
    myShow();
}