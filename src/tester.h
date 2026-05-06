#pragma once

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Wire.h>

#include "DeepSleepHandler.h"
#include "MeasurementCapture.h"
#include "RTCMemoryStorage.h"
#include "WS2812BLedMatrix.h"
#include "WiFiPowerManager.h"
#include "WireMeasurement.h"
#include "adc_calibrator.h"
#include "esp_task_wdt.h"
#include "resitancemeasurement.h"

// State enum
typedef enum { Waiting, EpeeTesting, FoilTesting, LameTesting, WireTesting_1, WireTesting_2, ReelTesting } State_t;
typedef enum {
    SHAPE_F,
    SHAPE_E,
    SHAPE_S,
    SHAPE_P,
    SHAPE_DIAMOND,
    SHAPE_SQUARE,
    SHAPE_R,
    SHAPE_GND,
    SHAPE_NONE
} Shapes_t;

// Timeout constants
constexpr int WIRE_TEST_1_TIMEOUT = 1;
constexpr int NO_WIRES_PLUGGED_IN_TIMEOUT = 2;
constexpr int NO_WIRES_PLUGGED_IN_TIMEOUT_REEL = 7;
constexpr int FOIL_TEST_TIMEOUT = 1000;
constexpr int WIRE_TEST_DELAY = 2000;  // 2 seconds delay after special test exit
constexpr int LOOP_DELAY_IN_WIRETESTING1 = 400;

class Tester {
   private:
    // State variables
    State_t currentState;
    int timeToSwitch;
    int noWireTimeout;
    bool allGood;
    unsigned long lastSpecialTestExit;
    Shapes_t ShowingShape = SHAPE_NONE;

    DeepSleepHandler myDeepSleepHandler;
    long StartForLowPower;
    int WaitingStateCounter;

    // Task handle
    TaskHandle_t testerTaskHandle;

    // LED Panel reference
    WS2812B_LedMatrix* ledPanel;
    u_int32_t DefaultBlinkColor;

    // Add reference values as class members (correct type: int)
    int myRefs_Ohm[11];  // 0..10 Ohm thresholds in mV
    int Ohm_20;
    int Ohm_50;

    // Mode-switching detection thresholds (fixed mV values, not resistance-based)
    static constexpr int ProbeConnectedThreshold = 500;  // BrCl below this → probe is connected
    static constexpr int EpeeTipContactThreshold = 600;  // ArCl below this → tip touching probe
    static constexpr int ShortDetectThreshold = 1500;    // ArBr/BrCr below this → cross-short
    static constexpr int WireConnectedThreshold = 2000;  // ArBr below this → body cord connected

    // Color breakpoints derived from settings (computed in UpdateThresholdsWithLeadResistance)
    // Lamé
    int Lame_Green, Lame_Yellow, Lame_Orange;
    // Foil body cord (loop/ArBr)
    int FoilLoop_Green, FoilLoop_Yellow, FoilLoop_Orange;
    // Foil/Epee tip wire (single wire/ArCl)
    int FoilTip_Green, FoilTip_Yellow, FoilTip_Orange;
    int FoilLeakThreshold;
    int EpeeTip_Green, EpeeTip_Yellow, EpeeTip_Orange;
    // Foil/Epee probe (BrCl) — shared
    int Probe_Green, Probe_Yellow, Probe_Orange;
    // Epee return wire (loop/ArCr)
    int EpeeLoop_Green, EpeeLoop_Yellow, EpeeLoop_Orange;
    int EpeeLeak;
    // Main wire test (body cord) — normal mode
    int Bodycord_Green, Bodycord_Yellow, Bodycord_Broken;
    // Main wire test — reel mode (×1, ×2, ×5 of ReelBodycordThreshold)
    int Reel_Green, Reel_Yellow, Reel_Broken;

    int ReferenceBroken = 0;  // Set by SetWiretestMode()
    int ReferenceGreen = 0;
    int ReferenceYellow = 0;
    int ReferenceOrange = 0;
    int ReferenceShort = 160;
    bool ReelMode = false;
    RTCMemoryStorage rtc;
    EmpiricalResistorCalibrator mycalibrator;

    // New measurement API
    MeasurementCapture Capture_;
    MeasurementSet currentMeasurements_;
    float leadresistances[3] = {0.0, 0.0, 0.0};
    float AverageLeadResistance = 0.0;
    bool IgnoreCalibrationWarning = false;
    // Private methods

    void doCommonReturnFromSpecialMode();
    bool delayAndTestWirePluggedIn(long delay);
    bool delayAndTestWirePluggedInFoil(long delay);
    bool delayAndTestWirePluggedInEpee(long delay);
    bool delayAndTestWirePluggedInEpeeAndShowConnectionValue(long delay, Terminal Tfrom, Terminal Tto);
    bool delayAndTestWirePluggedInLameTestTop(long delay);
    void doEpeeTest();
    void doFoilTest();
    void doFoilLeakTest();
    void doLameTest();
    void doLameTest_Top();
    void doReelTest();
    bool DebounceTest(int LowBound, int HighBound);
    bool animateSingleWire(const MeasurementSet& measurements, Terminal terminal);
    void SetWiretestMode(bool Reelmode);
    bool GetWiretestMode() { return ReelMode; };

    bool doQuickCheck(bool bClearAtTheEnd = true);
    void handleWaitingState();
    void handleWireTestingState1();
    void handleWireTestingState2();
    bool debouncedCondition(std::function<bool()> condition, int debounceMs = 10);

    // Static task wrapper
    static void testerTaskWrapper(void* parameter);

   public:
    // Constructor
    Tester(WS2812B_LedMatrix* ledPanelRef);

    // Destructor
    ~Tester();

    // Public methods
    void begin(bool ForceCalibration = false);
    void stop();
    void setIgnoreCalibrationWarning(bool value) { IgnoreCalibrationWarning = value; };
    State_t getState() const;
    void setState(State_t newState);
    bool isAllGood() const;

    // Calibration control
    void startCalibration();
    void stopCalibration();
    float get_v_gpio() const { return mycalibrator.get_v_gpio(); };
    float get_r1_r2() const { return mycalibrator.get_r1_r2(); };
    float get_correction() const { return mycalibrator.get_correction(); };
    void UpdateThresholdsWithLeadResistance(float RLead);
    float getAverageLeadResistance() const { return AverageLeadResistance; }

    // Main task loop
    void taskLoop();

    // Add method to set reference values (correct type: int)
    // void setReferenceValues(int* refs);

    // Add method to get reference values (for debugging)
    // int* getReferenceValues() const;
};

// Global instance declaration
extern Tester* testerInstance;
