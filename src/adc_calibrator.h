#pragma once

#include "CalibrationPaths.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

// Model used until a tester is calibrated
#if HARDWARE_REV == 3
// Pooled fit of two calibrated hw_rev3 testers on bidirectional readings (2026-10-08);
// within 1.8 % on both without their own calibration
constexpr float Default_v_gpio = 3.137;
constexpr float Default_r1_r2 = 92.173;
constexpr float Default_r_internal = 0.0925;
#else
constexpr float Default_v_gpio = 3.1290;
constexpr float Default_r1_r2 = 116.0;
constexpr float Default_r_internal = 0.0;
#endif

// Calibration verdict per point. The error counts relative to max(R, 1 Ohm), so below 1 Ohm it is
// judged in ohms (2 % = 20 mOhm). Up to 10 Ohm the target is 2 % and the limit 5 %; above 10 Ohm
// 5 % and 10 %. A point above its limit fails the calibration, which is then not saved.
constexpr float CalExcellentPercent = 2.0f;
constexpr float CalPassPercent = 5.0f;
constexpr float CalHighFromOhm = 10.0f;
constexpr float CalExcellentPercentHigh = 5.0f;
constexpr float CalPassPercentHigh = 10.0f;

constexpr int CalMinPoints = 4;
constexpr int CalMaxPoints = 8;

// Model M2: the tester sees the unknown R plus an internal series resistance Ri (PCB traces, vias,
// wiring to the sockets) between its sense points, driven through Rs (the two 33 Ohm resistors plus
// the GPIO drivers):  V_diff = V_gpio * (R + Ri) / (R + Ri + Rs)   ->   R = Rs * V / (V_gpio - V) - Ri
struct EmpiricalModel {
    float v_gpio;      // open-circuit drive voltage (V)
    float r1_r2;       // series resistance outside the sense points, Rs (Ohm)
    float r_internal;  // series resistance between the sense points, Ri (Ohm)
};

enum CalVerdict { CalExcellent, CalPass, CalFail };
const char* calVerdictName(CalVerdict verdict);

// How well a model reproduces a set of known resistors
struct CalEvaluation {
    float r_est[CalMaxPoints];    // resistance the model reads for each point (Ohm, -1 = no solution)
    float err_pct[CalMaxPoints];  // |r_est - r_ref| / max(r_ref, 1 Ohm) * 100
    float rms_mv;                 // RMS of model voltage - measured voltage
    float max_err_pct;
    CalVerdict verdict;
};

class EmpiricalResistorCalibrator {
   public:
    // Initialize with ADC channel for differential measurement
    bool begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom);

    // Interactive calibration over the serial port, on the default calibration path
    bool calibrate_interactively_empirical();

    // Resistance for a differential measurement (V); 0 for a short, -1 for open or invalid
    float get_resistance_empirical(float v_diff_measured) const;

    // Get threshold in millivolts — matches the unit returned by getDifferentialSample().
    int get_mv_threshold(float resistance_ohm, float lead_ohm = 0.0f) const;

    void DoFactoryReset() { setModel(factoryModel()); };

    // Measurement functions
    struct EmpiricalReading {
        float v_top;
        float v_bottom;
        float v_diff;
        float v_diff_sd_mv;  // standard deviation of the per-sample V_diff (mV)
        int samples_used;    // samples left after trimming outliers
        float resistance;
        // Half measured with the drive reversed: same channels, so here the bottom one is high
        // and v_diff_rev = v_bottom_rev - v_top_rev
        bool has_reversed;
        float v_top_rev;
        float v_bottom_rev;
        float v_diff_rev;
        float v_high;  // open-circuit reference for the model: the high side, both directions averaged
    };

    // Drive a calibration path and average `readings` calls of MeasurementHardware::getDifferentialSample,
    // the function every test uses, so the model is calibrated on exactly what the tests measure.
    // v_diff is that (bidirectional) reading; the forward and reversed halves are reported separately.
    // v_diff_sd_mv is the spread of the single readings.
    EmpiricalReading measure(const CalibrationPath& path, int readings = 64, bool verbose = true);

    // Model access
    EmpiricalModel model() const { return {v_gpio, r1_r2, r_internal}; }
    void setModel(const EmpiricalModel& m) {
        v_gpio = m.v_gpio;
        r1_r2 = m.r1_r2;
        r_internal = m.r_internal;
    }
    static EmpiricalModel factoryModel() { return {Default_v_gpio, Default_r1_r2, Default_r_internal}; }

    // Pure model maths, no hardware access. Voltages in V, resistances in Ohm.
    static float modelVoltage(const EmpiricalModel& m, float r_ohm);
    static float modelResistance(const EmpiricalModel& m, float v_diff);
    // Fit Rs and Ri to known resistors (tolerance-weighted least squares); v_gpio from the open circuit
    static EmpiricalModel fit(const float* r_ref, const float* v_diff, int n, float v_gpio_open);
    static CalEvaluation evaluate(const EmpiricalModel& m, const float* r_ref, const float* v_diff, int n);
    // Per point: error in % of max(R, 1 Ohm), and the target and limit for a resistor of that size
    static float scaledErrorPct(float r_est, float r_ref);
    static float targetPct(float r_ref) { return r_ref > CalHighFromOhm ? CalExcellentPercentHigh : CalExcellentPercent; }
    static float limitPct(float r_ref) { return r_ref > CalHighFromOhm ? CalPassPercentHigh : CalPassPercent; }

    // Getters for calibration parameters
    float get_v_gpio() const { return v_gpio; }
    float get_r1_r2() const { return r1_r2; }
    float get_r_internal() const { return r_internal; }

    // Check if calibrator is properly calibrated
    bool is_calibrated() const { return v_gpio > 0 && r1_r2 > 0; }

    // Roundtrip diagnostic: thresholds converted back to resistance should give R again
    void print_roundtrip_diagnostics(float lead_ohm = 0.0f);

   private:
    // ADC channels
    adc1_channel_t channel_top;
    adc1_channel_t channel_bottom;

    float v_gpio = Default_v_gpio;
    float r1_r2 = Default_r1_r2;
    float r_internal = Default_r_internal;

    // ADC calibration
    esp_adc_cal_characteristics_t adc_chars;

    void wait_for_enter();
    float read_float_from_uart();                  // ESP32-safe float input with WDT reset
    char read_char_from_uart(long timeout = 999);  // ESP32-safe char input with WDT reset

    static double weightedCost(const EmpiricalModel& m, const float* r_ref, const float* v_diff, int n);
    void show_calibration_quality(const float* R_values, const float* V_diff_values, int num_points);
};
