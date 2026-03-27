#pragma once

#include <Arduino.h>

#include "WS2812BLedMatrix.h"
// External variables that need to be shared
extern bool DoCalibration;
extern bool LowPowerMode;
extern WS2812B_LedMatrix* LedPanel;
extern int myRefs_Ohm[];  // Correct type: int array
extern int StoredRefs_ohm[];
extern int measurements[3][3];

// Weapon threshold settings (Ohm values, configurable via web UI)
extern float BodycordThreshold;
extern float ReelBodycordThreshold;
extern float FoilSingleWireThreshold;
extern float FoilLoopThreshold;
extern float FoilMassProbeThreshold;
extern float EpeeSingleWireThreshold;
extern float EpeeLoopThreshold;
extern float EpeeMassProbeThreshold;
extern float LameThreshold;

// Function declarations
extern void Calibrate();
extern void testWiresOnByOne();
extern int testArCr();
extern int testArBr();
extern int testBrCr();
extern bool testStraightOnly(int threshold);
