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
__attribute__((deprecated("Use MeasurementHardware::Set_IODirectionAndValue instead"))) void Set_IODirectionAndValue(
    uint8_t setting, uint8_t values);

__attribute__((deprecated("Use MeasurementHardware::init_AD instead"))) void init_AD();

// Use MeasurementCapture instead
__attribute__((deprecated("Use MeasurementCapture::captureMatrix3x3 instead"))) void testWiresOnByOne();

__attribute__((deprecated("Use MeasurementCapture::captureStraightOnly instead"))) bool testStraightOnly(
    int threashold = 160);

__attribute__((deprecated("Use MeasurementCapture::measureArBr instead"))) int testArBr();

__attribute__((deprecated("Use MeasurementCapture::measureArCr instead"))) int testArCr();

__attribute__((deprecated("Use MeasurementCapture::measureArCl instead"))) int testArCl();

__attribute__((deprecated("Use MeasurementCapture::measureBrCr instead"))) int testBrCr();

__attribute__((deprecated("Use MeasurementCapture::measureBrCl instead"))) int testBrCl();

__attribute__((deprecated("Use MeasurementCapture::measureCrCl instead"))) int testCrCl();

__attribute__((deprecated("Use MeasurementCapture::measureAlBl instead"))) int testAlBl();

// Use MeasurementAnalysis instead
__attribute__((deprecated("Use MeasurementAnalysis::isWirePluggedIn instead"))) bool WirePluggedIn(
    int threashold = 160);

__attribute__((deprecated("Use MeasurementAnalysis::isWirePluggedInFoil instead"))) bool WirePluggedInFoil(
    int threashold = 160);

__attribute__((deprecated("Use MeasurementAnalysis::isWirePluggedInEpee instead"))) bool WirePluggedInEpee(
    int threashold = 160);

__attribute__((deprecated("Use MeasurementAnalysis::isWirePluggedInLameTop instead"))) bool WirePluggedInLameTopTesting(
    int threashold = 160);

__attribute__((deprecated("Use MeasurementAnalysis::isBroken instead"))) bool IsBroken(int Nr, int threashold = 160);

__attribute__((deprecated("Use MeasurementAnalysis::isSwappedWith instead"))) bool IsSwappedWith(int i, int j,
                                                                                                 int threashold = 160);
