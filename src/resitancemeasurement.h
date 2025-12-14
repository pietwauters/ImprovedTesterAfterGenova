// Copyright (c) Piet Wauters 2022 <piet.wauters@gmail.com>
#pragma once

#include <Arduino.h>

#include "Hardware.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// New clean API - recommended for new code
#include "MeasurementAnalysis.h"
#include "MeasurementCapture.h"
#include "MeasurementHardware.h"

// ============================================================================
// LEGACY API - For backwards compatibility only
// These functions are deprecated - use the new API instead
// ============================================================================

// Global variables (extern declarations)
extern int measurements[3][3];
extern const int Reference_3_Ohm[3];
extern const int Reference_5_Ohm[3];
extern const int Reference_10_Ohm[3];

// Function prototypes - DEPRECATED
// Use MeasurementHardware::* instead
void Set_IODirectionAndValue(uint8_t setting, uint8_t values);
void init_AD();

// Use MeasurementCapture instead
void testWiresOnByOne();
bool testStraightOnly(int threashold = 160);
int testArBr();
int testArCr();
int testArCl();
int testBrCr();
int testBrCl();
int testCrCl();
int testAlBl();

// Use MeasurementAnalysis instead
bool WirePluggedIn(int threashold = 160);
bool WirePluggedInFoil(int threashold = 160);
bool WirePluggedInEpee(int threashold = 160);
bool WirePluggedInLameTopTesting(int threashold = 160);
bool IsBroken(int Nr, int threashold = 160);
bool IsSwappedWith(int i, int j, int threashold = 160);
