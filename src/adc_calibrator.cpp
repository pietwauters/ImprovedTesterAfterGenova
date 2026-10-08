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

EmpiricalResistorCalibrator::EmpiricalReading EmpiricalResistorCalibrator::measure(const CalibrationPath& path,
                                                                                   int readings, bool verbose) {
    const int samples_per_reading = 16;  // the default of getDifferentialSample, as the tests use it
    EmpiricalReading result = {};
    double fwd1 = 0, fwd2 = 0, rev1 = 0, rev2 = 0, sum = 0, sum_sq = 0;

    MeasurementHardware::Set_IODirectionAndValue(path.ioDirection, path.ioValues);
    vTaskDelay(pdMS_TO_TICKS(2));  // let the terminals settle after switching
    for (int i = 0; i < readings; i++) {
        esp_task_wdt_reset();
        MeasurementHardware::DifferentialDetail d;
        int mv = MeasurementHardware::getDifferentialSample(path.top, path.bottom, samples_per_reading, &d);
        sum += mv;
        sum_sq += (double)mv * mv;
        fwd1 += d.fwd_mv1;
        fwd2 += d.fwd_mv2;
        if (d.reversed) {
            result.has_reversed = true;
            rev1 += d.rev_mv1;
            rev2 += d.rev_mv2;
        }
        if (i < readings - 1) {
            vTaskDelay(pdMS_TO_TICKS(2));  // spread the readings over time, like a test does
        }
    }
    double mean = sum / readings;
    double var = sum_sq / readings - mean * mean;
    result.v_diff_sd_mv = var > 0 ? (float)sqrt(var) : 0.0f;
    result.samples_used = readings * samples_per_reading;

    // v_diff: the mean of the readings exactly as the tests get them (including their rounding to
    // whole mV), so the model converts what the tests measure. The halves are for diagnostics.
    result.v_diff = (float)(mean / 1000.0);
    result.v_top = (float)(fwd1 / readings / 1000.0);
    result.v_bottom = (float)(fwd2 / readings / 1000.0);
    if (result.has_reversed) {
        result.v_top_rev = (float)(rev1 / readings / 1000.0);
        result.v_bottom_rev = (float)(rev2 / readings / 1000.0);
        result.v_diff_rev = result.v_bottom_rev - result.v_top_rev;
        result.v_high = (result.v_top + result.v_bottom_rev) / 2.0f;
    } else {
        result.v_high = result.v_top;
    }

    if (verbose) {
        printf("Differential reading: v_diff=%.2f mV (spread of single readings %.2f mV, %d readings)\n",
               result.v_diff * 1000, result.v_diff_sd_mv, readings);
        printf("  Forward: v_top=%.1f mV, v_bottom=%.1f mV\n", result.v_top * 1000, result.v_bottom * 1000);
        if (result.has_reversed) {
            printf("  Reversed: v_top=%.1f mV, v_bottom=%.1f mV\n", result.v_top_rev * 1000,
                   result.v_bottom_rev * 1000);
        }
    }

    // For empirical model: V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
    result.resistance = get_resistance_empirical(result.v_diff);
    return result;
}

float EmpiricalResistorCalibrator::get_resistance_empirical(float v_diff_measured) const {
    return modelResistance(model(), v_diff_measured);
}

// R = Rs * V / (V_gpio - V) - Ri, clamped at 0 (a short reads 0, never negative)
float EmpiricalResistorCalibrator::modelResistance(const EmpiricalModel& m, float v_diff) {
    if (m.v_gpio <= 0 || m.r1_r2 <= 0 || v_diff >= m.v_gpio) {
        return -1.0f;  // not calibrated, or open / beyond the drive voltage
    }
    if (v_diff <= 0) {
        return 0.0f;
    }
    float r = m.r1_r2 * v_diff / (m.v_gpio - v_diff) - m.r_internal;
    return r > 0 ? r : 0.0f;
}

// V = V_gpio * (R + Ri) / (R + Ri + Rs)
float EmpiricalResistorCalibrator::modelVoltage(const EmpiricalModel& m, float r_ohm) {
    float r = r_ohm + m.r_internal;
    if (r <= 0) {
        return 0.0f;
    }
    return m.v_gpio * r / (r + m.r1_r2);
}

void EmpiricalResistorCalibrator::print_roundtrip_diagnostics(float lead_ohm) {
    const float test_R[] = {0.5f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f, 20.0f, 50.0f};
    const int n = (int)(sizeof(test_R) / sizeof(test_R[0]));

    printf("\n=== Threshold Roundtrip Diagnostics (lead=%.2f Ohm) ===\n", lead_ohm);
    printf("  R(Ohm) | thresh_mV | roundtrip_R | error_Ohm\n");
    printf("  -------+-----------+-------------+----------\n");
    for (int i = 0; i < n; i++) {
        float R = test_R[i];
        int thresh_mV = get_mv_threshold(R, lead_ohm);
        float roundtrip = get_resistance_empirical((float)thresh_mV / 1000.0f) - lead_ohm;
        printf("  %6.1f | %9d | %11.4f | %+.4f\n", R, thresh_mV, roundtrip, roundtrip - R);
    }
    printf("=== End Roundtrip Diagnostics (errors come from rounding to whole mV) ===\n\n");
}

int EmpiricalResistorCalibrator::get_mv_threshold(float resistance_ohm, float lead_ohm) const {
    float total_R = resistance_ohm + lead_ohm;
    if (total_R <= 0.0f)
        return 0;
    return (int)lroundf(modelVoltage(model(), total_R) * 1000.0f);
}

float EmpiricalResistorCalibrator::scaledErrorPct(float r_est, float r_ref) {
    return (r_est - r_ref) / fmaxf(r_ref, 1.0f) * 100.0f;
}

// Sum of squared errors, each relative to max(R, 1 Ohm) and divided by its target, so every point
// counts in proportion to how precise it has to be (2 % up to 10 Ohm, 5 % above). Uses the
// unclamped inverse so the cost stays smooth near 0.
double EmpiricalResistorCalibrator::weightedCost(const EmpiricalModel& m, const float* r_ref, const float* v_diff,
                                                 int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        if (v_diff[i] <= 0 || v_diff[i] >= m.v_gpio || m.r1_r2 <= 0) {
            return 1e12;
        }
        double r_est = (double)m.r1_r2 * v_diff[i] / (m.v_gpio - v_diff[i]) - m.r_internal;
        double e = (r_est - r_ref[i]) / fmax(r_ref[i], 1.0) * 100.0 / targetPct(r_ref[i]);
        sum += e * e;
    }
    return sum;
}

// Nelder-Mead over (Rs, Ri), V_gpio fixed, started from a plain divider (median Rs, Ri = 0)
EmpiricalModel EmpiricalResistorCalibrator::fit(const float* r_ref, const float* v_diff, int n, float v_gpio_open) {
    float seeds[CalMaxPoints];
    int ns = 0;
    for (int i = 0; i < n && i < CalMaxPoints; i++) {
        if (v_diff[i] > 0 && v_diff[i] < v_gpio_open) {
            seeds[ns++] = r_ref[i] * (v_gpio_open - v_diff[i]) / v_diff[i];
        }
    }
    for (int i = 1; i < ns; i++) {  // insertion sort for the median
        float k = seeds[i];
        int j = i - 1;
        while (j >= 0 && seeds[j] > k) {
            seeds[j + 1] = seeds[j];
            j--;
        }
        seeds[j + 1] = k;
    }
    double rs0 = ns > 0 ? seeds[ns / 2] : Default_r1_r2;
    auto cost = [&](double rs, double ri) {
        EmpiricalModel m = {v_gpio_open, (float)rs, (float)ri};
        return weightedCost(m, r_ref, v_diff, n);
    };

    double x[3][2] = {{rs0, 0.0}, {rs0 + 2.0, 0.0}, {rs0, 0.05}};
    double f[3];
    for (int round = 0; round < 2; round++) {  // second round restarts with a small simplex
        if (round == 1) {
            double bx = x[0][0], by = x[0][1];
            double init[3][2] = {{bx, by}, {bx + 0.2, by}, {bx, by + 0.005}};
            for (int i = 0; i < 3; i++) {
                x[i][0] = init[i][0];
                x[i][1] = init[i][1];
            }
        }
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
        // keep the best point in x[0]
        int best = 0;
        for (int i = 1; i < 3; i++) {
            if (f[i] < f[best]) {
                best = i;
            }
        }
        if (best != 0) {
            x[0][0] = x[best][0];
            x[0][1] = x[best][1];
            f[0] = f[best];
        }
    }
    return {v_gpio_open, (float)x[0][0], (float)x[0][1]};
}

CalEvaluation EmpiricalResistorCalibrator::evaluate(const EmpiricalModel& m, const float* r_ref, const float* v_diff,
                                                    int n) {
    CalEvaluation e = {};
    e.verdict = CalExcellent;
    float sum_sq_mv = 0.0f;
    for (int i = 0; i < n && i < CalMaxPoints; i++) {
        float error_mv = (modelVoltage(m, r_ref[i]) - v_diff[i]) * 1000.0f;
        sum_sq_mv += error_mv * error_mv;
        e.r_est[i] = modelResistance(m, v_diff[i]);
        // No solution counts as an infinitely bad point
        e.err_pct[i] = e.r_est[i] >= 0 ? fabsf(scaledErrorPct(e.r_est[i], r_ref[i])) : 1000.0f;
        if (e.err_pct[i] > e.max_err_pct) {
            e.max_err_pct = e.err_pct[i];
        }
        if (e.err_pct[i] > limitPct(r_ref[i])) {
            e.verdict = CalFail;
        } else if (e.err_pct[i] > targetPct(r_ref[i]) && e.verdict == CalExcellent) {
            e.verdict = CalPass;
        }
    }
    e.rms_mv = n > 0 ? sqrtf(sum_sq_mv / n) : 0.0f;
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
    printf("  Max error: %.2f%% of max(R, 1 Ohm) (target %.0f%%, limit %.0f%%; above %.0f Ohm %.0f%% and %.0f%%)\n",
           e.max_err_pct, CalExcellentPercent, CalPassPercent, CalHighFromOhm, CalExcellentPercentHigh,
           CalPassPercentHigh);
    printf("  Verdict: %s\n", calVerdictName(e.verdict));
}

bool EmpiricalResistorCalibrator::calibrate_interactively_empirical() {
    const CalibrationPath& path = kDefaultCalibrationPath;

    printf("\n=== EMPIRICAL RESISTANCE CALIBRATOR ===\n");
    printf("Model: V_diff = V_gpio * (R + Ri) / (R + Ri + Rs)\n");
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
    EmpiricalReading open_reading = measure(path, 64);
    float v_gpio_open = open_reading.v_high;  // high side as reference (both directions averaged)

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
    printf("Known resistor must be connected between socket %s and socket %s!\n\n", path.from, path.to);

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
        EmpiricalReading reading = measure(path, 64);

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
    printf("  Rs = %.2f Ω\n", fitted.r1_r2);
    printf("  Ri = %.3f Ω\n\n", fitted.r_internal);

    // Step 3: Result
    printf("=== STEP 3: RESULT ===\n");
    show_calibration_quality(R_values, V_diff_values, num_points);
    if (evaluate(fitted, R_values, V_diff_values, num_points).verdict == CalFail) {
        printf("\nCalibration FAILED: a point is outside its limit. Not saved.\n");
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
        EmpiricalReading test_reading = measure(path, 64);

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
            float error_percent = error_abs / fmaxf(actual_resistance, 1.0f) * 100.0f;
            printf("  Actual = %.2f Ω, Error = %.2f Ω (%.1f%%)\n", actual_resistance, error_abs, error_percent);

            if (error_percent <= targetPct(actual_resistance)) {
                printf("  ✓ EXCELLENT: Error <= %.0f%%\n", targetPct(actual_resistance));
            } else if (error_percent <= limitPct(actual_resistance)) {
                printf("  ✓ PASS: Error <= %.0f%%\n", limitPct(actual_resistance));
            } else {
                printf("  ✗ FAIL: Error > %.0f%% - Consider recalibration\n", limitPct(actual_resistance));
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
