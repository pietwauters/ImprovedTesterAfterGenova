#include "resitancemeasurement.h"

#include <Arduino.h>

#include "MeasurementAnalysis.h"
#include "MeasurementCapture.h"
#include "MeasurementHardware.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_task_wdt.h"

// ============================================================================
// Legacy global variable - for backwards compatibility
// ============================================================================
int measurements[3][3];

// Reference constants
const int Reference_3_Ohm[] = {3 * 16, 3 * 16, 3 * 16};
const int Reference_5_Ohm[] = {5 * 16, 5 * 16, 5 * 16};
const int Reference_10_Ohm[] = {10 * 16, 10 * 16, 10 * 16};

// Static instance of capture class for legacy API
static MeasurementCapture s_capture;

// ============================================================================
// Legacy wrapper functions - delegate to new API
// ============================================================================

// Hardware delegation
void Set_IODirectionAndValue(uint8_t setting, uint8_t values) {
    MeasurementHardware::Set_IODirectionAndValue(setting, values);
}

void init_AD() { MeasurementHardware::init_AD(); }

// Capture delegation
void testWiresOnByOne() { s_capture.captureMatrix3x3Legacy(measurements); }

bool testStraightOnly(int threshold) { return s_capture.captureStraightOnlyLegacy(measurements, threshold); }

int testArBr() { return s_capture.measureArBr(); }

int testArCr() { return s_capture.measureArCr(); }

int testArCl() { return s_capture.measureArCl(); }

int testBrCr() { return s_capture.measureBrCr(); }

int testBrCl() { return s_capture.measureBrCl(); }

int testCrCl() { return s_capture.measureCrCl(); }

int testAlBl() { return s_capture.measureAlBl(); }

// Analysis delegation
bool WirePluggedIn(int threshold) { return MeasurementAnalysis::isWirePluggedInLegacy(measurements, threshold); }

bool WirePluggedInFoil(int threshold) {
    return MeasurementAnalysis::isWirePluggedInFoilLegacy(measurements, threshold);
}

bool WirePluggedInEpee(int threshold) {
    return MeasurementAnalysis::isWirePluggedInEpeeLegacy(measurements, threshold);
}

bool WirePluggedInLameTopTesting(int threshold) {
    return MeasurementAnalysis::isWirePluggedInLameTopLegacy(measurements, threshold);
}

bool IsBroken(int Nr, int threshold) { return MeasurementAnalysis::isBrokenLegacy(measurements, Nr, threshold); }

bool IsSwappedWith(int i, int j, int threshold) {
    return MeasurementAnalysis::isSwappedWithLegacy(measurements, i, j, threshold);
}