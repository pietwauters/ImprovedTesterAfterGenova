#pragma once

#include <Arduino.h>

#include "WS2812BLedMatrix.h"
#include "adc_calibrator.h"
#include "esp_task_wdt.h"
#include "resitancemeasurement.h"

// State enum
typedef enum { Waiting, EpeeTesting, FoilTesting, LameTesting, WireTesting_1, WireTesting_2, ReelTesting } State_t;
typedef enum { SHAPE_F, SHAPE_E, SHAPE_S, SHAPE_P, SHAPE_DIAMOND, SHAPE_SQUARE, SHAPE_R, SHAPE_NONE } Shapes_t;

// Timeout constants
constexpr int WIRE_TEST_1_TIMEOUT = 3;
constexpr int NO_WIRES_PLUGGED_IN_TIMEOUT = 2;
constexpr int FOIL_TEST_TIMEOUT = 1000;
constexpr int WIRE_TEST_DELAY = 2000;  // 2 seconds delay after special test exit

class Tester {
   private:
    // State variables
    State_t currentState;
    int timeToSwitch;
    int noWireTimeout;
    bool allGood;
    unsigned long lastSpecialTestExit;
    Shapes_t ShowingShape = SHAPE_NONE;

    // Task handle
    TaskHandle_t testerTaskHandle;

    // LED Panel reference
    WS2812B_LedMatrix* ledPanel;

    // Add reference values as class members (correct type: int)
    int myRefs_Ohm[11];  // Pointer to reference values
    int Ohm_20;
    int Ohm_30;
    int Ohm_50;
    int ReferenceBroken = myRefs_Ohm[10];
    int ReferenceGreen = myRefs_Ohm[1];
    int ReferenceYellow = myRefs_Ohm[3];
    int ReferenceOrange = myRefs_Ohm[10];
    int ReferenceShort = 160;
    bool ReelMode = false;

    EmpiricalResistorCalibrator mycalibrator;
    float leadresistances[3] = {0.0, 0.0, 0.0};
    float AverageLeadResistance = 0.0;
    bool IgnoreCalibrationWarning = false;
    // Private methods

    void doCommonReturnFromSpecialMode();
    bool delayAndTestWirePluggedIn(long delay);
    bool delayAndTestWirePluggedInFoil(long delay);
    bool delayAndTestWirePluggedInEpee(long delay);
    void doEpeeTest();
    void doFoilTest();
    void doLameTest();
    void doLameTest_Top();
    void doReelTest();
    bool animateSingleWire(int wireIndex, bool ReelMode = false);
    void SetWiretestMode(bool Reelmode);
    bool GetWiretestMode() { return ReelMode; };

    bool doQuickCheck();
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
    void begin();
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

    // Main task loop
    void taskLoop();

    // Add method to set reference values (correct type: int)
    // void setReferenceValues(int* refs);

    // Add method to get reference values (for debugging)
    // int* getReferenceValues() const;
};

// Global instance declaration
extern Tester* testerInstance;
