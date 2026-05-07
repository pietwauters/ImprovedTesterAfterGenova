#pragma once

#include "Hardware.h"
#include "MeasurementHardware.h"
#include "WireMeasurement.h"

// High-level measurement orchestration
// This layer knows WHAT to measure and HOW to combine hardware operations
// but delegates actual ADC reading to MeasurementHardware

class MeasurementCapture {
   public:
    // Capture all 15 possible measurements between 6 terminals
    // Fills the provided MeasurementSet with complete measurement data
    void captureAll(MeasurementSet& result);

    // Capture full 3x3 matrix (Cr/Ar/Br vs Cl/Al/Bl)
    // Fills the provided MeasurementSet with 9 measurements
    void captureMatrix3x3(MeasurementSet& result);

    // Capture straight-through connections only (with high-resolution sampling)
    // Fills result with Ar-Ar, Br-Br, Cr-Cr measurements
    void captureStraightOnly(MeasurementSet& result);

    // Update only straight-through measurements without clearing the set
    // Returns true if all straight connections are below threshold
    // Preserves existing cross-measurements in the result
    bool updateStraightOnly(MeasurementSet& result, int threshold);

    // Individual measurement functions - return millivolts
    int measureArBr();
    int measureArCr();
    int measureArCl();
    int measureBrCr();
    int measureBrCl();
    int measureCrCl();
    int measureAlBl();  // Actually measures Cl-Al (piste)

    // Capture a single measurement by terminal enum
    int captureSingle(Terminal from, Terminal to);

    // Legacy compatibility - populate global measurements[][] array
    void captureMatrix3x3Legacy(int measurements[3][3]);
    bool captureStraightOnlyLegacy(int measurements[3][3], int threshold);

    // Populate MeasurementSet from legacy measurements[][] array
    // This avoids double-measurement and ensures both APIs see identical data
    void populateFromLegacyArray(MeasurementSet& result, const int measurements[3][3]);
};
