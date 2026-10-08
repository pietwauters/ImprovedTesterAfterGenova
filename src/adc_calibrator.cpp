#include "adc_calibrator.h"

#include <Arduino.h>
#include <driver/uart.h>
#include <math.h>
#include <string.h>

#include "MeasurementHardware.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_vfs_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

// ========================================================================
// EMPIRICAL RESISTOR CALIBRATOR IMPLEMENTATION
// ========================================================================

bool EmpiricalResistorCalibrator::begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom) {
    this->channel_top = adc_channel_top;
    this->channel_bottom = adc_channel_bottom;

    // printf("Empirical calibrator: Configuring ADC channels top=%d, bottom=%d\n", channel_top, channel_bottom);

    // Configure ADC - same as differential calibrator
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(this->channel_top, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(this->channel_bottom, ADC_ATTEN_DB_11);

    // Initialize calibration - same as differential calibrator
    esp_adc_cal_value_t cal_type =
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    // printf("ADC initialized - channels %d and %d\n", channel_top, channel_bottom);
    printf("ADC calibration type: %d, vref: %d mV, coeff_a: %d, coeff_b: %d\n", cal_type, adc_chars.vref,
           adc_chars.coeff_a, adc_chars.coeff_b);
    return true;
}


const char* calVerdictName(CalVerdict verdict) {
    switch (verdict) {
        case CalExcellent:
            return "excellent";
        case CalPass:
            return "pass";
        default:
            return "fail";
    }
}

// Trimmed means of both channels over `samples` samples, plus the spread of the per-sample difference
EmpiricalResistorCalibrator::ChannelMeans EmpiricalResistorCalibrator::sampleChannels(adc1_channel_t top,
                                                                                      adc1_channel_t bottom,
                                                                                      int samples) {
    ChannelMeans result;
    // Use the same successful approach as the working differential calibrator
    const float trim_percent = 0.2f;  // Remove 20% outliers like the working differential calibrator

    // Allocate arrays for calibrated voltage samples (in millivolts)
    uint32_t* mv_top_samples = new uint32_t[samples];
    uint32_t* mv_bottom_samples = new uint32_t[samples];

    // Take samples from both channels and convert to millivolts immediately
    double sum_diff = 0.0, sum_diff_sq = 0.0;
    for (int i = 0; i < samples; ++i) {
        esp_task_wdt_reset();  // Reset WDT every iteration

        uint32_t raw_top = adc1_get_raw(top);
        uint32_t raw_bottom = adc1_get_raw(bottom);

        // Convert each raw sample to millivolts using eFuse calibration
        mv_top_samples[i] = esp_adc_cal_raw_to_voltage(raw_top, &adc_chars);
        mv_bottom_samples[i] = esp_adc_cal_raw_to_voltage(raw_bottom, &adc_chars);
        double diff = (double)mv_top_samples[i] - (double)mv_bottom_samples[i];
        sum_diff += diff;
        sum_diff_sq += diff * diff;
        if (i < samples - 1)
            vTaskDelay(pdMS_TO_TICKS(1));
    }
    double mean_diff = sum_diff / samples;
    double var_diff = sum_diff_sq / samples - mean_diff * mean_diff;
    result.diff_sd_mv = var_diff > 0 ? (float)sqrt(var_diff) : 0.0f;

    // Sort arrays to identify outliers (sorting millivolt values now)
    for (int i = 0; i < samples - 1; i++) {
        for (int j = 0; j < samples - i - 1; j++) {
            if (mv_top_samples[j] > mv_top_samples[j + 1]) {
                uint32_t temp = mv_top_samples[j];
                mv_top_samples[j] = mv_top_samples[j + 1];
                mv_top_samples[j + 1] = temp;
            }
            if (mv_bottom_samples[j] > mv_bottom_samples[j + 1]) {
                uint32_t temp = mv_bottom_samples[j];
                mv_bottom_samples[j] = mv_bottom_samples[j + 1];
                mv_bottom_samples[j + 1] = temp;
            }
        }
    }

    // Calculate how many samples to trim from each end
    int trim_count = (int)(samples * trim_percent / 2.0f);  // Divide by 2 since we trim both ends
    int start_index = trim_count;
    int end_index = samples - trim_count;
    int valid_samples = end_index - start_index;

    // Calculate trimmed mean of calibrated millivolt values
    uint64_t sum_mv_top = 0, sum_mv_bottom = 0;
    for (int i = start_index; i < end_index; i++) {
        sum_mv_top += mv_top_samples[i];
        sum_mv_bottom += mv_bottom_samples[i];
    }

    // Clean up arrays
    delete[] mv_top_samples;
    delete[] mv_bottom_samples;

    // Convert millivolts to volts (keep the fraction of a mV the averaging gives)
    result.v_top = (float)sum_mv_top / valid_samples / 1000.0f;
    result.v_bottom = (float)sum_mv_bottom / valid_samples / 1000.0f;
    result.used = valid_samples;
    return result;
}

// The drive with the other of the two output pins high; 0 if the path does not drive exactly two pins
static uint8_t reversedValues(const CalibrationPath& path) {
    uint8_t outputs = (uint8_t)(~path.ioDirection) & 0x7F;  // 7 driver pins, a 0 bit is an output
    if (__builtin_popcount(outputs) != 2 || __builtin_popcount(outputs & path.ioValues) != 1) {
        return 0;
    }
    return outputs & (uint8_t)~path.ioValues;
}

EmpiricalResistorCalibrator::EmpiricalReading EmpiricalResistorCalibrator::measure(const CalibrationPath& path,
                                                                                   int samples, bool verbose,
                                                                                   bool bidirectional) {
    EmpiricalReading result = {};
    uint8_t reversed = bidirectional ? reversedValues(path) : 0;
    int forwardSamples = reversed != 0 ? samples / 2 : samples;

    MeasurementHardware::Set_IODirectionAndValue(path.ioDirection, path.ioValues);
    vTaskDelay(pdMS_TO_TICKS(2));  // let the terminals settle after switching
    ChannelMeans fwd = sampleChannels(path.top, path.bottom, forwardSamples);
    result.v_top = fwd.v_top;
    result.v_bottom = fwd.v_bottom;
    result.v_diff = fwd.v_top - fwd.v_bottom;
    result.v_diff_sd_mv = fwd.diff_sd_mv;
    result.samples_used = fwd.used;

    if (reversed != 0) {
        // Same channels, current the other way: the bottom terminal is now the high one
        MeasurementHardware::Set_IODirectionAndValue(path.ioDirection, reversed);
        vTaskDelay(pdMS_TO_TICKS(2));
        ChannelMeans rev = sampleChannels(path.top, path.bottom, samples - forwardSamples);
        result.has_reversed = true;
        result.v_top_rev = rev.v_top;
        result.v_bottom_rev = rev.v_bottom;
        result.v_diff_rev = rev.v_bottom - rev.v_top;
        MeasurementHardware::Set_IODirectionAndValue(path.ioDirection, path.ioValues);
    }

    if (verbose) {
        printf("Empirical differential result (trimmed mean): v_top=%.4f V, v_bottom=%.4f V, v_diff=%.4f V\n",
               result.v_top, result.v_bottom, result.v_diff);
        printf("  Used %d samples, V_diff spread %.2f mV\n", result.samples_used, result.v_diff_sd_mv);
        if (result.has_reversed) {
            printf("  Reversed: v_top=%.4f V, v_bottom=%.4f V, v_diff=%.4f V\n", result.v_top_rev,
                   result.v_bottom_rev, result.v_diff_rev);
        }
    }

    // For empirical model: V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
    // The model is calibrated on the forward reading, the one the tester uses in its tests
    result.resistance = get_resistance_empirical(result.v_diff);

    return result;
}

float EmpiricalResistorCalibrator::get_resistance_empirical(float v_diff_measured) const {
    return modelResistance(model(), v_diff_measured);
}

float EmpiricalResistorCalibrator::modelResistance(const EmpiricalModel& m, float v_diff_measured) {
    if (m.v_gpio <= 0 || m.r1_r2 <= 0) {
        return -1.0f;  // Not calibrated
    }

    if (v_diff_measured <= 0 || v_diff_measured >= m.v_gpio) {
        return -1.0f;  // Invalid measurement
    }

    // Solve empirical model: V_diff_measured = V_gpio * R / (R + R1_R2 + Correction/R)
    // This is a quadratic equation in R. Rearranging:
    // V_diff_measured * (R + R1_R2 + Correction/R) = V_gpio * R
    // Multiply by R: V_diff_measured * R² + V_diff_measured * R1_R2 * R + V_diff_measured * Correction = V_gpio * R²
    // Rearrange: (V_diff_measured - V_gpio) * R² + V_diff_measured * R1_R2 * R + V_diff_measured * Correction = 0
    // Standard form: a*R^2 + b*R + c = 0

    float a = v_diff_measured - m.v_gpio;  // This is negative since v_diff < v_gpio
    float b = v_diff_measured * m.r1_r2;
    float c = v_diff_measured * m.correction;

    if (fabs(a) < 1e-9) {
        return -1.0f;  // Degenerate case
    }

    // Quadratic formula: R = (-b ± sqrt(b^2 - 4ac)) / (2a)
    // Note: a = (v_diff - v_gpio) is always negative since v_diff < v_gpio
    float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0) {
        return -1.0f;  // No real solution
    }

    float sqrt_discriminant = sqrtf(discriminant);

    // Numerically stable solution
    float q = -0.5f * (b + (b > 0 ? sqrt_discriminant : -sqrt_discriminant));
    float r1 = q / a;
    float r2 = c / q;

    // Choose the positive solution
    // Since a < 0, when dividing by 2a, signs flip
    // We want the physically meaningful positive root (typically the larger one)
    if (r1 > 0 && r2 > 0) {
        return fmaxf(r1, r2);  // Choose LARGER positive root (since a is negative)
    } else if (r1 > 0) {
        return r1;
    } else if (r2 > 0) {
        return r2;
    } else {
        return -1.0f;  // No positive solution
    }
}

float EmpiricalResistorCalibrator::modelVoltage(const EmpiricalModel& m, float R_known) {
    if (R_known <= 0.001f)  // Use small threshold instead of exact zero to avoid division issues
        return 0.0f;
    return m.v_gpio * R_known / (R_known + m.r1_r2 + m.correction / R_known);
}

uint32_t EmpiricalResistorCalibrator::get_adc_threshold_for_resistance_with_leads(float resistance_threshold,
                                                                                  float lead_resistance) {
    if (v_gpio <= 0 || r1_r2 <= 0) {
        return 0;  // Not calibrated
    }

    float total_resistance = resistance_threshold + lead_resistance;
    if (total_resistance <= 0.001f) {  // Use small threshold to avoid division by zero issues
        return 0;                      // Invalid input
    }

    // Step 1: Calculate expected V_diff using empirical model
    float expected_v_diff = calculate_model_voltage(total_resistance, v_gpio, r1_r2, correction);
    if (expected_v_diff <= 0) {
        return 0;  // Invalid calculation
    }

    // Step 2: Use classical voltage divider with equivalent R1 and R2
    // From empirical model: effective R1 = R2 = (r1_r2 + correction/R_unknown) / 2
    // IMPORTANT: Handle the case where total_resistance is very small to avoid division by zero
    float correction_term = (total_resistance > 0.001f) ? (correction / total_resistance) : 0.0f;

    float r1_equivalent = (r1_r2 + correction_term) / 2.0f;
    float r2_equivalent = r1_equivalent;  // R1 ≈ R2

    // Step 3: Calculate V_top and V_bottom using voltage divider equations
    // Total circuit: V_gpio across (R1 + R_unknown + R2)
    float total_circuit_resistance = r1_equivalent + total_resistance + r2_equivalent;

    // V_bottom = V_gpio * R2 / (R1 + R_unknown + R2)
    float v_bottom_expected = v_gpio * r2_equivalent / total_circuit_resistance;

    // V_top = V_gpio * (R_unknown + R2) / (R1 + R_unknown + R2)
    float v_top_expected = v_gpio * (total_resistance + r2_equivalent) / total_circuit_resistance;

    // Verify: V_diff = V_top - V_bottom should match our empirical model result
    float calculated_v_diff = v_top_expected - v_bottom_expected;

    // Step 4: Convert each voltage to ADC raw value separately
    int raw_top = voltage_to_adc_raw(v_top_expected);
    int raw_bottom = voltage_to_adc_raw(v_bottom_expected);

    // Step 5: Return the difference of raw values
    int raw_diff = raw_top - raw_bottom;

    return (uint32_t)(raw_diff > 0 ? raw_diff : 0);  // Ensure non-negative result
}

void EmpiricalResistorCalibrator::print_roundtrip_diagnostics(float lead_ohm) {
    const float test_R[] = {1.0f, 2.0f, 3.0f, 5.0f, 10.0f, 20.0f, 25.0f, 30.0f, 50.0f};
    const int n = (int)(sizeof(test_R) / sizeof(test_R[0]));

    printf("\n=== Threshold Roundtrip Diagnostics (lead=%.2f Ohm) ===\n", lead_ohm);
    printf("  R(Ohm) | thresh_mV | roundtrip_R | error_Ohm\n");
    printf("  -------+-----------+-------------+----------\n");

    // thresh_mV   : get_mv_threshold() — same unit as getDifferentialSample()
    // roundtrip_R : thresh_mV/1000 -> get_resistance_empirical() — should recover R exactly
    // error_Ohm   : roundtrip_R - R — should be ~0.00
    for (int i = 0; i < n; i++) {
        float R = test_R[i];
        int thresh_mV = get_mv_threshold(R, lead_ohm);
        float roundtrip = get_resistance_empirical((float)thresh_mV / 1000.0f) - lead_ohm;
        printf("  %6.1f | %9d | %11.4f | %+.4f\n", R, thresh_mV, roundtrip, roundtrip - R);
    }

    printf("\n  All errors should be ~0.00 Ohm (units now match).\n");
    printf("=== End Roundtrip Diagnostics ===\n\n");
}

int EmpiricalResistorCalibrator::get_mv_threshold(float resistance_ohm, float lead_ohm) const {
    float total_R = resistance_ohm + lead_ohm;
    if (total_R <= 0.001f)
        return 0;
    return (int)(calculate_model_voltage(total_R, v_gpio, r1_r2, correction) * 1000.0f);
}

float EmpiricalResistorCalibrator::calculate_model_voltage(float R_known, float v_gpio, float r1_r2,
                                                           float correction) const {
    if (R_known <= 0.001f)  // Use small threshold instead of exact zero to avoid division issues
        return 0.0f;
    return v_gpio * R_known / (R_known + r1_r2 + correction / R_known);
}

// Helper function to convert voltage to ADC raw value using binary search

int EmpiricalResistorCalibrator::voltage_to_adc_raw(float voltage) {
    // Binary search to find ADC value that gives closest voltage
    // Same approach as DifferentialResistorCalibrator
    int low = 0, high = 4095, result = 0;
    while (low <= high) {
        int mid = (low + high) / 2;
        float v = esp_adc_cal_raw_to_voltage(mid, &adc_chars) / 1000.0f;
        if (v < voltage) {
            low = mid + 1;
            result = mid;
        } else {
            high = mid - 1;
        }
    }
    return result;
}

// Golden section search: finds the minimum of a unimodal function on [a, b] to within tol.
// Converges in ~log(tol/(b-a)) / log(0.618) evaluations — far fewer than a linear sweep.
template <typename CostFn>
static float golden_min(CostFn cost, float a, float b, float tol = 0.05f) {
    constexpr float phi = 0.6180339887f;  // (sqrt(5)-1)/2
    float c = b - phi * (b - a);
    float d = a + phi * (b - a);
    float fc = cost(c), fd = cost(d);
    while (b - a > tol) {
        if (fc < fd) {
            b = d;
            d = c;
            fd = fc;
            c = b - phi * (b - a);
            fc = cost(c);
        } else {
            a = c;
            c = d;
            fc = fd;
            d = a + phi * (b - a);
            fd = cost(d);
        }
    }
    return 0.5f * (a + b);
}

// Helper function for weighted slope optimization using relative errors
// Uses hybrid weighting to balance accuracy across the full resistance range
float EmpiricalResistorCalibrator::optimize_slope_weighted(const float* R_values, const float* V_diff_values,
                                                           int num_points, float v_gpio_open, float correction) {
    auto cost = [&](float r1_r2_test) {
        EmpiricalModel m = {v_gpio_open, r1_r2_test, correction};
        float weighted_error = 0.0f, total_weight = 0.0f;
        for (int i = 0; i < num_points; i++) {
            float V_predicted = modelVoltage(m, R_values[i]);
            float relative_error = (V_predicted - V_diff_values[i]) / V_diff_values[i];
            // Hybrid weighting: bias toward high R where R1_R2 has most effect
            float weight = R_values[i] + 0.5f / (R_values[i] + 0.1f);
            weighted_error += weight * relative_error * relative_error;
            total_weight += weight;
        }
        return total_weight > 0 ? weighted_error / total_weight : 1e10f;
    };
    return golden_min(cost, 70.0f, 200.0f);
}

// Helper function for correction factor sweep optimization using relative errors
// Emphasizes low R values where correction term has maximum effect
float EmpiricalResistorCalibrator::optimize_correction_sweep(const float* R_values, const float* V_diff_values,
                                                             int num_points, float v_gpio_open, float r1_r2_fixed) {
    auto cost = [&](float correction_test) {
        EmpiricalModel m = {v_gpio_open, r1_r2_fixed, correction_test};
        float weighted_error = 0.0f, total_weight = 0.0f;
        for (int i = 0; i < num_points; i++) {
            float V_predicted = modelVoltage(m, R_values[i]);
            float relative_error = (V_predicted - V_diff_values[i]) / V_diff_values[i];
            // Inverse weighting: emphasize low R where Correction/R term dominates
            float weight = 1.0f / (R_values[i] + 0.1f);
            weighted_error += weight * relative_error * relative_error;
            total_weight += weight;
        }
        return total_weight > 0 ? weighted_error / total_weight : 1e10f;
    };
    return golden_min(cost, -100.0f, 150.0f);
}

// Iterative refinement: R1_R2 and Correction are coupled, so optimize them in turn.
// Fits relative voltage errors; used as the starting point of fit().
EmpiricalModel EmpiricalResistorCalibrator::fitVoltage(const float* r_ref, const float* v_diff, int n,
                                                       float v_gpio_open) {
    EmpiricalModel m = {v_gpio_open, 100.0f, 0.0f};  // initial guess, no correction
    const int max_iterations = 10;
    for (int iter = 0; iter < max_iterations; iter++) {
        m.r1_r2 = optimize_slope_weighted(r_ref, v_diff, n, v_gpio_open, m.correction);
        m.correction = optimize_correction_sweep(r_ref, v_diff, n, v_gpio_open, m.r1_r2);
    }
    return m;
}

// Sum of squared relative resistance errors; a point without a solution costs as much as 1000 % off
double EmpiricalResistorCalibrator::resistanceCost(const EmpiricalModel& m, const float* r_ref, const float* v_diff,
                                                   int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        float r_est = modelResistance(m, v_diff[i]);
        double e = r_est > 0 ? (r_est - r_ref[i]) / r_ref[i] : 10.0;
        sum += e * e;
    }
    return sum;
}

// Least squares on the relative resistance error over (R1_R2, Correction), V_gpio fixed.
// Nelder-Mead simplex started from the voltage fit; never returns a worse model than that.
EmpiricalModel EmpiricalResistorCalibrator::fit(const float* r_ref, const float* v_diff, int n, float v_gpio_open) {
    EmpiricalModel start = fitVoltage(r_ref, v_diff, n, v_gpio_open);
    auto cost = [&](double r1_r2, double correction) {
        EmpiricalModel m = {v_gpio_open, (float)r1_r2, (float)correction};
        return resistanceCost(m, r_ref, v_diff, n);
    };

    // Simplex of 3 points in (R1_R2, Correction)
    double x[3][2] = {{start.r1_r2, start.correction},
                      {start.r1_r2 + 2.0, start.correction},
                      {start.r1_r2, start.correction + 2.0}};
    double f[3];
    for (int i = 0; i < 3; i++) {
        f[i] = cost(x[i][0], x[i][1]);
    }
    for (int iter = 0; iter < 500; iter++) {
        // order: x[0] best, x[2] worst
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 2 - i; j++) {
                if (f[j] > f[j + 1]) {
                    double tf = f[j];
                    f[j] = f[j + 1];
                    f[j + 1] = tf;
                    for (int k = 0; k < 2; k++) {
                        double t = x[j][k];
                        x[j][k] = x[j + 1][k];
                        x[j + 1][k] = t;
                    }
                }
            }
        }
        if (f[2] - f[0] < 1e-12) {
            break;
        }
        double c[2], xr[2], xe[2], xc[2];
        for (int k = 0; k < 2; k++) {
            c[k] = (x[0][k] + x[1][k]) / 2.0;
            xr[k] = c[k] + (c[k] - x[2][k]);
        }
        double fr = cost(xr[0], xr[1]);
        if (fr < f[0]) {
            for (int k = 0; k < 2; k++) {
                xe[k] = c[k] + 2.0 * (c[k] - x[2][k]);
            }
            double fe = cost(xe[0], xe[1]);
            const double* best = fe < fr ? xe : xr;
            x[2][0] = best[0];
            x[2][1] = best[1];
            f[2] = fe < fr ? fe : fr;
        } else if (fr < f[1]) {
            x[2][0] = xr[0];
            x[2][1] = xr[1];
            f[2] = fr;
        } else {
            for (int k = 0; k < 2; k++) {
                xc[k] = c[k] + 0.5 * (x[2][k] - c[k]);
            }
            double fc = cost(xc[0], xc[1]);
            if (fc < f[2]) {
                x[2][0] = xc[0];
                x[2][1] = xc[1];
                f[2] = fc;
            } else {  // shrink towards the best point
                for (int i = 1; i < 3; i++) {
                    for (int k = 0; k < 2; k++) {
                        x[i][k] = x[0][k] + 0.5 * (x[i][k] - x[0][k]);
                    }
                    f[i] = cost(x[i][0], x[i][1]);
                }
            }
        }
    }
    int best = 0;
    for (int i = 1; i < 3; i++) {
        if (f[i] < f[best]) {
            best = i;
        }
    }
    if (f[best] >= cost(start.r1_r2, start.correction)) {
        return start;
    }
    return {v_gpio_open, (float)x[best][0], (float)x[best][1]};
}

CalEvaluation EmpiricalResistorCalibrator::evaluate(const EmpiricalModel& m, const float* r_ref, const float* v_diff,
                                                    int n) {
    CalEvaluation e = {};
    float sum_sq_mv = 0.0f;
    for (int i = 0; i < n && i < CalMaxPoints; i++) {
        float error_mv = (modelVoltage(m, r_ref[i]) - v_diff[i]) * 1000.0f;
        sum_sq_mv += error_mv * error_mv;
        e.r_est[i] = modelResistance(m, v_diff[i]);
        // No solution counts as an infinitely bad point
        e.err_pct[i] = e.r_est[i] > 0 ? fabsf(e.r_est[i] - r_ref[i]) / r_ref[i] * 100.0f : 1000.0f;
        if (e.err_pct[i] > e.max_err_pct) {
            e.max_err_pct = e.err_pct[i];
        }
    }
    e.rms_mv = n > 0 ? sqrtf(sum_sq_mv / n) : 0.0f;
    if (e.max_err_pct <= CalExcellentPercent) {
        e.verdict = CalExcellent;
    } else if (e.max_err_pct <= CalPassPercent) {
        e.verdict = CalPass;
    } else {
        e.verdict = CalFail;
    }
    return e;
}

// Print how well the active model reproduces the calibration points
void EmpiricalResistorCalibrator::show_calibration_quality(const float* R_values, const float* V_diff_values,
                                                           int num_points) {
    CalEvaluation e = evaluate(model(), R_values, V_diff_values, num_points);

    printf("Calibration Point Verification:\n");
    printf("R_actual   V_measured   V_model   R_model   Error_%%\n");
    printf("------------------------------------------------------\n");
    for (int i = 0; i < num_points; i++) {
        printf("%7.2f    %8.1f     %7.1f   %7.3f    %5.2f\n", R_values[i], V_diff_values[i] * 1000,
               modelVoltage(model(), R_values[i]) * 1000, e.r_est[i], e.err_pct[i]);
    }
    printf("------------------------------------------------------\n");
    printf("  RMS Error: %.2f mV\n", e.rms_mv);
    printf("  Max resistance error: %.2f%% (target %.0f%%, limit %.0f%%)\n", e.max_err_pct, CalExcellentPercent,
           CalPassPercent);
    printf("  Verdict: %s\n", calVerdictName(e.verdict));
}

bool EmpiricalResistorCalibrator::calibrate_interactively_empirical() {
    const CalibrationPath& path = kDefaultCalibrationPath;

    printf("\n=== EMPIRICAL RESISTANCE CALIBRATOR ===\n");
    printf("This calibrator uses the empirical model:\n");
    printf("V_diff = V_gpio_open * R / (R + R1_R2 + Correction/R)\n");
    printf("Calibration path: %s\n\n", path.name);

    // Step 0: Measure open circuit reference voltage
    printf("=== STEP 0: REFERENCE MEASUREMENT ===\n");
    printf("Disconnect ALL resistors from the circuit.\n");
    printf("Press ENTER when ready to measure open circuit voltage: ");
    fflush(stdout);

    if (read_char_from_uart(10) == 'q') {
        return false;
    }

    printf("Measuring open circuit voltage...\n");
    EmpiricalReading open_reading = measure(path, 50);
    float v_gpio_open = open_reading.v_top;  // Use top voltage as reference

    printf("Open circuit measurements:\n");
    printf("  V_gpio_open = %.1f mV (reference voltage)\n", v_gpio_open * 1000);
    printf("  V_bottom = %.1f mV\n", open_reading.v_bottom * 1000);
    printf("  V_diff = %.1f mV (should be close to V_gpio_open)\n\n", open_reading.v_diff * 1000);

    if (v_gpio_open < 2.5f || v_gpio_open > 3.6f) {
        printf("WARNING: V_gpio_open = %.1fV is outside expected range (2.5-3.6V)\n", v_gpio_open);
        printf("Check power supply and connections.\n\n");
    }

    // Step 1: Data collection
    printf("=== STEP 1: DATA COLLECTION ===\n");
    float R_values[CalMaxPoints];
    float V_diff_values[CalMaxPoints];
    int num_points = 0;

    printf("Now collect calibration data points with known resistors.\n");
    printf("Suggest: 1, 2, 3, 5, 8, 10, 12 ohms for good coverage.\n\n");
    printf("Known resistor must be connected between socket %c on top and socket %c on the bottom!\n\n", path.socket,
           path.socket);

    while (num_points < CalMaxPoints) {
        printf("[Point %d] Enter known resistance value (0 to finish, need minimum %d): ", num_points + 1,
               CalMinPoints);
        fflush(stdout);

        float R_known = read_float_from_uart();

        if (R_known <= 0) {
            if (num_points >= CalMinPoints) {
                break;
            } else {
                printf("Need at least %d calibration points for multi-stage fitting!\n", CalMinPoints);
                continue;
            }
        }

        printf("Connect %.2f Ω resistor and press ENTER...\n", R_known);
        wait_for_enter();

        // Measure differential voltage
        EmpiricalReading reading = measure(path, 100);

        printf("Measured: R=%.2fΩ → V_diff=%.1fmV (V_top=%.1fmV, V_bottom=%.1fmV)\n", R_known, reading.v_diff * 1000,
               reading.v_top * 1000, reading.v_bottom * 1000);

        if (reading.v_diff <= 0 || reading.v_diff > v_gpio_open) {
            printf("Invalid measurement! V_diff should be between 0 and %.1fmV. Try again.\n", v_gpio_open * 1000);
            continue;
        }

        R_values[num_points] = R_known;
        V_diff_values[num_points] = reading.v_diff;
        num_points++;
        printf("\n");
    }

    printf("Collected %d calibration points.\n\n", num_points);

    // Step 2: Fit
    printf("=== STEP 2: PARAMETER OPTIMIZATION ===\n");
    EmpiricalModel fitted = fit(R_values, V_diff_values, num_points, v_gpio_open);
    setModel(fitted);
    printf("  V_gpio_open = %.1f mV\n", fitted.v_gpio * 1000);
    printf("  R1_R2 = %.1f Ω\n", fitted.r1_r2);
    printf("  Correction = %.1f Ω²\n\n", fitted.correction);

    // Step 3: Result
    printf("=== STEP 3: RESULT ===\n");
    show_calibration_quality(R_values, V_diff_values, num_points);
    if (evaluate(fitted, R_values, V_diff_values, num_points).verdict == CalFail) {
        printf("\nCalibration FAILED: a point is more than %.0f%% off. Not saved.\n", CalPassPercent);
        return false;
    }

    // Interactive verification phase - test with different resistors
    printf("\n=== EMPIRICAL CALIBRATION VERIFICATION ===\n");
    printf("Test the calibration with different resistors.\n");
    printf("The model will calculate resistance from measured voltage.\n");
    printf("Press 'q' to quit verification, ENTER to test a resistor.\n\n");

    while (true) {
        printf("Connect test resistor and press ENTER (or 'q' to quit): ");
        fflush(stdout);
        char key = read_char_from_uart();

        if (key == 'q' || key == 'Q') {
            printf("Verification complete.\n");
            break;
        }

        // Measure the unknown resistor
        printf("Measuring unknown resistor...\n");
        EmpiricalReading test_reading = measure(path, 100);

        if (test_reading.v_diff <= 0) {
            printf("Invalid reading: V_diff=%.1fmV. Check connections.\n", test_reading.v_diff * 1000);
            continue;
        }

        // Calculate resistance using empirical model
        float calculated_resistance = get_resistance_empirical(test_reading.v_diff);

        printf("Measurements:\n");
        printf("  V_top = %.1f mV\n", test_reading.v_top * 1000);
        printf("  V_bottom = %.1f mV\n", test_reading.v_bottom * 1000);
        printf("  V_diff = %.1f mV\n", test_reading.v_diff * 1000);
        printf("  Calculated Resistance = %.2f Ω\n", calculated_resistance);

        if (calculated_resistance < 0) {
            printf("  ERROR: Invalid resistance calculation. Check measurement range.\n");
        } else if (calculated_resistance > 15.0f) {
            printf("  WARNING: Resistance above typical 0-15Ω range.\n");
        }

        // Ask for known value to compare accuracy
        printf("Enter actual resistance value for accuracy check (0 to skip): ");
        fflush(stdout);
        float actual_resistance = read_float_from_uart();

        if (actual_resistance > 0) {
            float error_abs = fabs(calculated_resistance - actual_resistance);
            float error_percent = (error_abs / actual_resistance) * 100.0f;
            printf("  Actual = %.2f Ω, Error = %.2f Ω (%.1f%%)\n", actual_resistance, error_abs, error_percent);

            if (error_percent <= CalExcellentPercent) {
                printf("  ✓ EXCELLENT: Error <= %.0f%%\n", CalExcellentPercent);
            } else if (error_percent <= CalPassPercent) {
                printf("  ✓ PASS: Error <= %.0f%%\n", CalPassPercent);
            } else {
                printf("  ✗ FAIL: Error > %.0f%% - Consider recalibration\n", CalPassPercent);
            }
        }
        printf("\n");
    }

    return true;
}


void EmpiricalResistorCalibrator::wait_for_enter() {
    printf("Press ENTER to continue...");
    while (getchar() != '\n');
}

float EmpiricalResistorCalibrator::read_float_from_uart() {
    char buffer[32];
    int buffer_pos = 0;

    // Clear input buffer
    uart_flush_input(UART_NUM_0);

    // Show cursor prompt
    printf(">> ");
    fflush(stdout);

    while (buffer_pos < sizeof(buffer) - 1) {
        esp_task_wdt_reset();  // Reset WDT while waiting

        uint8_t c;
        int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            if (c == '\n' || c == '\r') {
                buffer[buffer_pos] = '\0';
                printf("\n");
                fflush(stdout);

                // Convert string to float
                if (buffer_pos > 0) {
                    char* endptr;
                    float value = strtof(buffer, &endptr);
                    if (endptr == buffer || endptr == NULL) {
                        printf("Invalid input '%s'. Please enter a number.\n", buffer);
                        fflush(stdout);
                        return -1.0f;
                    }
                    printf("Entered: %.2f\n", value);
                    fflush(stdout);
                    return value;
                } else {
                    printf("No input. Please enter a number.\n");
                    fflush(stdout);
                    return -1.0f;
                }
            } else if (c == '\b' || c == 127) {  // Backspace
                if (buffer_pos > 0) {
                    buffer_pos--;
                    printf("\b \b");  // Erase character on screen
                    fflush(stdout);
                }
            } else if (c >= '0' && c <= '9' || c == '.' || c == '-') {  // Valid number characters only
                if (buffer_pos < sizeof(buffer) - 1) {
                    buffer[buffer_pos] = c;
                    buffer_pos++;
                    printf("%c", c);  // Echo character immediately
                    fflush(stdout);
                }
            } else if (c >= ' ' && c <= '~') {  // Other printable characters - ignore but show feedback
                printf("\a");                   // Bell sound for invalid character
                fflush(stdout);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Buffer full fallback
    buffer[buffer_pos] = '\0';
    printf("\nBuffer full. Using: %s\n", buffer);
    fflush(stdout);

    char* endptr;
    float value = strtof(buffer, &endptr);
    if (endptr == buffer) {
        printf("Invalid input.\n");
        fflush(stdout);
        return -1.0f;
    }

    return value;
}

char EmpiricalResistorCalibrator::read_char_from_uart(long timeout) {
    // Clear input buffer
    uart_flush_input(UART_NUM_0);

    // Show cursor prompt
    printf(">> ");
    fflush(stdout);
    long start_time = millis();
    while (true) {
        esp_task_wdt_reset();  // Reset WDT while waiting

        uint8_t c;
        int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            if (c == '\n' || c == '\r') {
                printf("\n");
                fflush(stdout);
                return '\n';                    // Return newline as entered
            } else if (c >= ' ' && c <= '~') {  // Printable characters
                printf("%c\n", c);              // Echo character and newline
                fflush(stdout);
                return (char)c;
            }
            // Ignore non-printable characters
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        if (millis() > start_time + timeout * 1000) {
            return 'q';
        }
    }
}
bool EmpiricalResistorCalibrator::save_calibration_to_nvs(const char* nvs_namespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("Failed to open NVS namespace '%s' for writing: %s\n", nvs_namespace, esp_err_to_name(err));
        return false;
    }

    // Save empirical model parameters
    err |= nvs_set_blob(handle, "v_gpio", &v_gpio, sizeof(float));
    err |= nvs_set_blob(handle, "r1_r2", &r1_r2, sizeof(float));
    err |= nvs_set_blob(handle, "correction", &correction, sizeof(float));

    // Save version for future compatibility
    int version = CurrentVersion;
    err |= nvs_set_blob(handle, "Version", &version, sizeof(int));

    err |= nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK) {
        printf("Empirical calibration saved to NVS: V_gpio=%.1fmV, R1_R2=%.1fΩ, Correction=%.1fΩ²\n", v_gpio * 1000,
               r1_r2, correction);
        return true;
    } else {
        printf("Failed to save empirical calibration to NVS: %s\n", esp_err_to_name(err));
        return false;
    }
}

bool EmpiricalResistorCalibrator::load_calibration_from_nvs(const char* nvs_namespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        printf("No empirical calibration found in NVS namespace '%s': %s\n", nvs_namespace, esp_err_to_name(err));
        return false;
    }

    size_t required_size = sizeof(float);
    size_t required_int_size = sizeof(int);
    int version = 0;

    // Load all calibration parameters
    err = nvs_get_blob(handle, "v_gpio", &v_gpio, &required_size);
    if (err != ESP_OK)
        goto load_failed;

    required_size = sizeof(float);
    err = nvs_get_blob(handle, "r1_r2", &r1_r2, &required_size);
    if (err != ESP_OK)
        goto load_failed;

    required_size = sizeof(float);
    err = nvs_get_blob(handle, "correction", &correction, &required_size);
    if (err != ESP_OK)
        goto load_failed;

    err = nvs_get_blob(handle, "Version", &version, &required_int_size);
    if (err != ESP_OK)
        goto load_failed;

    nvs_close(handle);

    // Check version compatibility
    if (version < CurrentVersion) {
        printf("Empirical calibration version %d is outdated (current: %d). Please recalibrate.\n", version,
               CurrentVersion);
        return false;
    }

    printf("Empirical calibration loaded from NVS: V_gpio=%.1fmV, R1_R2=%.1fΩ, Correction=%.1fΩ²\n", v_gpio * 1000,
           r1_r2, correction);
    return true;

load_failed:
    nvs_close(handle);
    printf("Failed to load empirical calibration from NVS: %s\n", esp_err_to_name(err));
    return false;
}