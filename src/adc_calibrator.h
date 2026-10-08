#pragma once

#include "CalibrationPaths.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

constexpr int CurrentVersion = 3;
constexpr float Default_v_gpio = 3.1290;
constexpr float Default_r1_r2 = 116.0;
constexpr float Default_correction = 1.0;

// Calibration verdict limits: every point's resistance error must stay within these
constexpr float CalExcellentPercent = 2.0f;  // target
constexpr float CalPassPercent = 5.0f;       // above this the calibration fails and is not saved

constexpr int CalMinPoints = 4;
constexpr int CalMaxPoints = 8;

// Parameters of the empirical model V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
struct EmpiricalModel {
    float v_gpio;      // Effective GPIO voltage (V)
    float r1_r2;       // Combined fixed resistance (Ohm)
    float correction;  // Current-dependent correction factor (Ohm^2)
};

enum CalVerdict { CalExcellent, CalPass, CalFail };
const char* calVerdictName(CalVerdict verdict);

// How well a model reproduces a set of known resistors
struct CalEvaluation {
    float r_est[CalMaxPoints];    // resistance the model reads for each point (Ohm, -1 = no solution)
    float err_pct[CalMaxPoints];  // |r_est - r_ref| / r_ref * 100
    float rms_mv;                 // RMS of model voltage - measured voltage
    float max_err_pct;
    CalVerdict verdict;
};

// Empirical resistor calibrator using your proven model:
// V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
class EmpiricalResistorCalibrator {
   public:
    // Initialize with ADC channel for differential measurement
    bool begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom);

    // Interactive calibration over the serial port, on the default calibration path
    bool calibrate_interactively_empirical();

    // Get resistance from differential measurement using empirical model
    float get_resistance_empirical(float v_diff_measured) const;

    // Get threshold in millivolts — matches the unit returned by getDifferentialSample().
    // Use this for all threshold comparisons (replaces get_adc_threshold_for_resistance_with_leads).
    int get_mv_threshold(float resistance_ohm, float lead_ohm = 0.0f) const;

    // DEPRECATED: returns raw ADC counts, not millivolts — unit mismatch with getDifferentialSample().
    uint32_t get_adc_threshold_for_resistance_with_leads(float resistance_threshold, float lead_resistance = 0.0f);

    // Legacy single-model storage (namespace "emp_cal"); CalibrationStore migrates from it
    bool save_calibration_to_nvs(const char* nvs_namespace = "emp_cal");
    bool load_calibration_from_nvs(const char* nvs_namespace = "emp_cal");
    void DoFactoryReset() { setModel(factoryModel()); };

    // Measurement functions
    struct EmpiricalReading {
        float v_top;
        float v_bottom;
        float v_diff;
        float v_diff_sd_mv;  // standard deviation of the per-sample V_diff (mV)
        int samples_used;    // samples left after trimming outliers
        float resistance;
        // Second half with the drive reversed (bidirectional only): same channels, so here
        // the bottom one is high and v_diff_rev = v_bottom_rev - v_top_rev
        bool has_reversed;
        float v_top_rev;
        float v_bottom_rev;
        float v_diff_rev;
    };

    // Drive the terminals of a calibration path and measure it. Bidirectional: the first half of
    // the samples forward, the second half with the current reversed (both reported separately;
    // v_diff and resistance are always the forward reading, which is what the tests measure).
    EmpiricalReading measure(const CalibrationPath& path, int samples = 100, bool verbose = true,
                             bool bidirectional = false);

    // Model access
    EmpiricalModel model() const { return {v_gpio, r1_r2, correction}; }
    void setModel(const EmpiricalModel& m) {
        v_gpio = m.v_gpio;
        r1_r2 = m.r1_r2;
        correction = m.correction;
    }
    static EmpiricalModel factoryModel() { return {Default_v_gpio, Default_r1_r2, Default_correction}; }

    // Pure model maths, no hardware access. Voltages in V, resistances in Ohm.
    static float modelVoltage(const EmpiricalModel& m, float r_ohm);
    static float modelResistance(const EmpiricalModel& m, float v_diff);
    // Fit the model to known resistors; v_gpio is taken from the open-circuit reading.
    // Minimises the squared relative resistance error, the quantity the verdict judges.
    static EmpiricalModel fit(const float* r_ref, const float* v_diff, int n, float v_gpio_open);
    static CalEvaluation evaluate(const EmpiricalModel& m, const float* r_ref, const float* v_diff, int n);

    // Getters for calibration parameters
    float get_v_gpio() const { return v_gpio; }
    float get_r1_r2() const { return r1_r2; }
    float get_correction() const { return correction; }

    // Check if calibrator is properly calibrated
    bool is_calibrated() const { return v_gpio > 0 && r1_r2 > 0; }

    // Roundtrip diagnostic: shows whether thresholds and measurements share the same unit.
    // Prints for each R: expected model mV, raw ADC threshold, ratio, and both roundtrips back to Ω.
    // If ratio ≈ 1.0 → units match; if ratio ≈ 1.05 → raw ADC ≠ mV (known unit mismatch).
    void print_roundtrip_diagnostics(float lead_ohm = 0.0f);

   private:
    // ADC channels
    adc1_channel_t channel_top;
    adc1_channel_t channel_bottom;

    // Empirical model parameters: V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
    float v_gpio = Default_v_gpio;          // Effective GPIO voltage
    float r1_r2 = Default_r1_r2;            // Combined fixed resistance
    float correction = Default_correction;  // Current-dependent correction factor

    // ADC calibration
    esp_adc_cal_characteristics_t adc_chars;

    struct ChannelMeans {
        float v_top;       // V, trimmed mean
        float v_bottom;    // V, trimmed mean
        float diff_sd_mv;  // spread of the per-sample top - bottom
        int used;          // samples after trimming
    };
    ChannelMeans sampleChannels(adc1_channel_t top, adc1_channel_t bottom, int samples);

    // Helper functions
    float calculate_model_voltage(float R_known, float v_gpio, float r1_r2, float correction) const;
    int voltage_to_adc_raw(float voltage);  // Convert voltage to ADC raw value
    void wait_for_enter();
    float read_float_from_uart();                  // ESP32-safe float input with WDT reset
    char read_char_from_uart(long timeout = 999);  // ESP32-safe char input with WDT reset

    // Multi-stage calibration helper functions
    static EmpiricalModel fitVoltage(const float* r_ref, const float* v_diff, int n, float v_gpio_open);
    static double resistanceCost(const EmpiricalModel& m, const float* r_ref, const float* v_diff, int n);
    static float optimize_slope_weighted(const float* R_values, const float* V_diff_values, int num_points,
                                         float v_gpio_open, float correction);
    static float optimize_correction_sweep(const float* R_values, const float* V_diff_values, int num_points,
                                           float v_gpio_open, float r1_r2_fixed);
    void show_calibration_quality(const float* R_values, const float* V_diff_values, int num_points);
};
