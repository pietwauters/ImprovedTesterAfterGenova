#pragma once

#include <string.h>

#include "Hardware.h"
#include "driver/adc.h"

// A measurement path the calibrator can measure a known resistor on: which
// terminals are driven (IODirection_* / IOValues_*) and which two ADC channels
// give V_diff = V(top) - V(bottom). The pin numbers behind these come from
// Hardware.h, so every hardware revision uses the same table.
//
// Path names use the terminal names of the code (Ar..Cl). Those differ from the
// FIE socket letters printed on the tester: code C is socket A, code A is
// socket B, code B is socket C. `socket` is what the operator is told.
struct CalibrationPath {
    const char* name;  // as used in the web API and the NVS keys, e.g. "Cl-Cr"
    char socket;       // FIE socket letter, top and bottom, to connect the resistor to
    uint8_t ioDirection;
    uint8_t ioValues;
    adc1_channel_t top;
    adc1_channel_t bottom;
};

static const CalibrationPath kCalibrationPaths[] = {
    // Resistor between socket A top and bottom (same drive and channels as MeasurementCapture::measureCrCl)
    {"Cl-Cr", 'A', IODirection_cr_cl, IOValues_cr_cl, cr_analog, cl_analog},
    // Resistor between socket C top and bottom (what the serial calibration measured before the web wizard)
    {"Bl-Br", 'C', IODirection_br_bl, IOValues_br_bl, br_analog, bl_analog},
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
