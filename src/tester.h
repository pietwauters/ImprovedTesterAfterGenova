#ifndef TESTER_H
#define TESTER_H

#include <Arduino.h>
#include "WS2812BLedMatrix.h"
#include "resitancemeasurement.h"
#include "esp_task_wdt.h"

// State enum
typedef enum {
    Waiting,
    EpeeTesting,
    FoilTesting,
    LameTesting,
    WireTesting_1,
    WireTesting_2
} State_t;

// Timeout constants
#define WIRE_TEST_1_TIMEOUT 50
#define NO_WIRES_PLUGGED_IN_TIMEOUT 2
#define FOIL_TEST_TIMEOUT 1000
#define WIRE_TEST_DELAY 2000  // 2 seconds delay after special test exit

class Tester {
private:
    // State variables
    State_t currentState;
    int timeToSwitch;
    int noWireTimeout;
    bool allGood;
    unsigned long lastSpecialTestExit;
    
    // Task handle
    TaskHandle_t testerTaskHandle;
    
    // LED Panel reference
    WS2812B_LedMatrix* ledPanel;
    
    // Add reference values as class members (correct type: int)
    int* myRefs_Ohm;  // Pointer to reference values
    
    // Private methods
    void doCommonReturnFromSpecialMode();
    bool delayAndTestWirePluggedIn(long delay);
    bool delayAndTestWirePluggedInFoil(long delay);
    bool delayAndTestWirePluggedInEpee(long delay);
    void doEpeeTest();
    void doFoilTest();
    void doLameTest();
    bool animateSingleWire(int wireIndex);
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
    State_t getState() const;
    void setState(State_t newState);
    bool isAllGood() const;
    
    // Calibration control
    void startCalibration();
    void stopCalibration();
    
    // Main task loop
    void taskLoop();
    
    // Add method to set reference values (correct type: int)
    void setReferenceValues(int* refs);
    
    // Add method to get reference values (for debugging)
    int* getReferenceValues() const;
};

// Global instance declaration
extern Tester* testerInstance;

#endif // TESTER_H