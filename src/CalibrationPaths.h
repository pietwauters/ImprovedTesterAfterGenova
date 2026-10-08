#pragma once

#include <string.h>

#include "Hardware.h"
#include "driver/adc.h"

// A measurement path the calibrator can measure a known resistor on: which
// terminals are driven (IODirection_* / IOValues_*) and which two ADC channels
// give V_diff = V(top) - V(bottom). The pin numbers behind these come from
// Hardware.h, so every hardware revision uses the same table. The drive
// configurations are the ones MeasurementCapture uses for the same pair; code
// terminal Al is driven and sensed as "piste" (on hw_rev2/3 they share a pin).
//
// Path names use the terminal names of the code (Ar..Cl). Those differ from the
// FIE socket letters printed on the tester: code C is socket A, code A is
// socket B, code B is socket C. `from` and `to` are what the operator is told.
struct CalibrationPath {
    const char* name;  // as used in the web API and the NVS keys, e.g. "Cl-Cr"
    const char* from;  // physical end points, e.g. "A top", "A bottom"
    const char* to;
    uint8_t ioDirection;
    uint8_t ioValues;
    adc1_channel_t top;     // channel of the driven-high terminal (forward)
    adc1_channel_t bottom;  // channel of the driven-low terminal (forward)
};

static const CalibrationPath kCalibrationPaths[] = {
    // Straight: the body cord wires. Cl-Cr is the default (calibrated) path; Bl-Br is what the
    // serial calibration measured before the web wizard
    {"Cl-Cr", "A top", "A bottom", IODirection_cr_cl, IOValues_cr_cl, cr_analog, cl_analog},
    {"Al-Ar", "B top", "B bottom", IODirection_ar_piste, IOValues_ar_piste, ar_analog, piste_analog},
    {"Bl-Br", "C top", "C bottom", IODirection_br_bl, IOValues_br_bl, br_analog, bl_analog},
    // Bottom pairs: epee loop, foil loop, bottom lame
    {"Ar-Cr", "B bottom", "A bottom", IODirection_ar_cr, IOValues_ar_cr, ar_analog, cr_analog},
    {"Ar-Br", "B bottom", "C bottom", IODirection_ar_br, IOValues_ar_br, ar_analog, br_analog},
    {"Br-Cr", "C bottom", "A bottom", IODirection_br_cr, IOValues_br_cr, br_analog, cr_analog},
    // Cross pairs used by the weapon tests: tip wire, probe
    {"Ar-Cl", "B bottom", "A top", IODirection_ar_cl, IOValues_ar_cl, ar_analog, cl_analog},
    {"Br-Cl", "C bottom", "A top", IODirection_br_cl, IOValues_br_cl, br_analog, cl_analog},
    // Other cross pairs (mode detection only)
    {"Cr-Al", "A bottom", "B top", IODirection_cr_piste, IOValues_cr_piste, cr_analog, piste_analog},
    {"Cr-Bl", "A bottom", "C top", IODirection_cr_bl, IOValues_cr_bl, cr_analog, bl_analog},
    {"Ar-Bl", "B bottom", "C top", IODirection_ar_bl, IOValues_ar_bl, ar_analog, bl_analog},
    {"Br-Al", "C bottom", "B top", IODirection_br_piste, IOValues_br_piste, br_analog, piste_analog},
    // Top pairs (mode detection only)
    {"Cl-Al", "A top", "B top", IODirection_cl_piste, IOValues_cl_piste, cl_analog, piste_analog},
    {"Cl-Bl", "A top", "C top", IODirection_bl_cl, IOValues_bl_cl, bl_analog, cl_analog},
    {"Al-Bl", "B top", "C top", IODirection_bl_piste, IOValues_bl_piste, bl_analog, piste_analog},
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
