#pragma once

#include <string.h>

#include "Hardware.h"
#include "driver/adc.h"

// A measurement path the calibrator can measure a known resistor on: which
// terminals are driven (IODirection_* / IOValues_*) and which two ADC channels
// give V_diff = V(top) - V(bottom). The pin numbers behind these come from
// Hardware.h, so every hardware revision uses the same table.
struct CalibrationPath {
    const char* name;  // as used in the web API and the NVS keys, e.g. "Cl-Cr"
    uint8_t ioDirection;
    uint8_t ioValues;
    adc1_channel_t top;
    adc1_channel_t bottom;
};

static const CalibrationPath kCalibrationPaths[] = {
    // Resistor between top C and bottom C (same drive and channels as MeasurementCapture::measureCrCl)
    {"Cl-Cr", IODirection_cr_cl, IOValues_cr_cl, cr_analog, cl_analog},
    // Resistor between top B and bottom B (what the serial calibration measured before the web wizard)
    {"Bl-Br", IODirection_br_bl, IOValues_br_bl, br_analog, bl_analog},
};
static const int kNumCalibrationPaths = sizeof(kCalibrationPaths) / sizeof(kCalibrationPaths[0]);

// The path whose model the tester uses for all thresholds (one model in firmware for now)
static const CalibrationPath& kDefaultCalibrationPath = kCalibrationPaths[0];

// Returns nullptr for an unknown name
inline const CalibrationPath* findCalibrationPath(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < kNumCalibrationPaths; i++) {
        if (strcmp(kCalibrationPaths[i].name, name) == 0) {
            return &kCalibrationPaths[i];
        }
    }
    return nullptr;
}
