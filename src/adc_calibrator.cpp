#include "adc_calibrator.h"

#include <Arduino.h>
#include <driver/uart.h>
#include <math.h>
#include <string.h>

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

EmpiricalResistorCalibrator::EmpiricalReading EmpiricalResistorCalibrator::read_differential_empirical(int samples) {
    EmpiricalReading result;
    // Use the same successful approach as the working differential calibrator
    const float trim_percent = 0.2f;  // Remove 20% outliers like the working differential calibrator

    // Allocate arrays for calibrated voltage samples (in millivolts)
    uint32_t* mv_top_samples = new uint32_t[samples];
    uint32_t* mv_bottom_samples = new uint32_t[samples];

    // Take samples from both channels and convert to millivolts immediately
    for (int i = 0; i < samples; ++i) {
        esp_task_wdt_reset();  // Reset WDT every iteration

        uint32_t raw_top = adc1_get_raw(channel_top);
        uint32_t raw_bottom = adc1_get_raw(channel_bottom);

        // Convert each raw sample to millivolts using eFuse calibration
        mv_top_samples[i] = esp_adc_cal_raw_to_voltage(raw_top, &adc_chars);
        mv_bottom_samples[i] = esp_adc_cal_raw_to_voltage(raw_bottom, &adc_chars);
        if (i < samples - 1)
            vTaskDelay(pdMS_TO_TICKS(1));
    }

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

    uint32_t avg_mv_top = sum_mv_top / valid_samples;
    uint32_t avg_mv_bottom = sum_mv_bottom / valid_samples;

    // Clean up arrays
    delete[] mv_top_samples;
    delete[] mv_bottom_samples;

    // Convert millivolts to volts
    result.v_top = avg_mv_top / 1000.0f;
    result.v_bottom = avg_mv_bottom / 1000.0f;
    result.v_diff = result.v_top - result.v_bottom;

    printf("Empirical differential result (trimmed mean): v_top=%.3f V, v_bottom=%.3f V, v_diff=%.3f V\n", result.v_top,
           result.v_bottom, result.v_diff);
    printf("  Used %d samples (removed %d outliers from each end)\n", valid_samples, trim_count);

    // DEBUG: Show final calculated values to help diagnose 0mV readings
    printf("DEBUG: Final averages - top=%lumV, bottom=%lumV, difference=%ldmV\n", avg_mv_top, avg_mv_bottom,
           (long)(avg_mv_top - avg_mv_bottom));

    // For empirical model: V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
    // The V_diff in this model is the voltage ACROSS the unknown resistor
    // Calculate resistance using empirical model with v_diff as the measured voltage
    result.resistance = get_resistance_empirical(result.v_diff);

    return result;
}

float EmpiricalResistorCalibrator::get_resistance_empirical(float v_diff_measured) const {
    if (v_gpio <= 0 || r1_r2 <= 0) {
        return -1.0f;  // Not calibrated
    }

    if (v_diff_measured <= 0 || v_diff_measured >= v_gpio) {
        return -1.0f;  // Invalid measurement
    }

    // Solve empirical model: V_diff_measured = V_gpio * R / (R + R1_R2 + Correction/R)
    // This is a quadratic equation in R. Rearranging:
    // V_diff_measured * (R + R1_R2 + Correction/R) = V_gpio * R
    // Multiply by R: V_diff_measured * R² + V_diff_measured * R1_R2 * R + V_diff_measured * Correction = V_gpio * R²
    // Rearrange: (V_diff_measured - V_gpio) * R² + V_diff_measured * R1_R2 * R + V_diff_measured * Correction = 0
    // Standard form: a*R^2 + b*R + c = 0

    float a = v_diff_measured - v_gpio;  // This is negative since v_diff < v_gpio
    float b = v_diff_measured * r1_r2;
    float c = v_diff_measured * correction;

    if (fabs(a) < 1e-9) {
        return -1.0f;  // Degenerate case
    }

    // Quadratic formula: R = (-b ± sqrt(b^2 - 4ac)) / (2a)
    // Note: a = (v_diff - v_gpio) is always negative since v_diff < v_gpio
    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        return -1.0f;  // No real solution
    }

    float sqrt_discriminant = sqrtf(discriminant);
    float r1 = (-b + sqrt_discriminant) / (2 * a);
    float r2 = (-b - sqrt_discriminant) / (2 * a);

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

float EmpiricalResistorCalibrator::calculate_model_voltage(float R_known, float v_gpio, float r1_r2, float correction) {
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

// Helper function to convert voltage back to resistance using empirical model
// Solves: V_diff = V_gpio * R / (R + R1_R2 + correction/R) for R
float EmpiricalResistorCalibrator::voltage_to_resistance(float v_diff, float v_gpio, float r1_r2, float correction) {
    if (v_diff <= 0 || v_gpio <= 0)
        return -1.0f;

    // From V_diff = V_gpio * R / (R + R1_R2 + Correction/R)
    // Rearranging: V_diff * (R + R1_R2 + Correction/R) = V_gpio * R
    // Multiplying by R: V_diff * R^2 + V_diff * R1_R2 * R + V_diff * Correction = V_gpio * R^2
    // Rearranged: (V_diff - V_gpio) * R^2 + V_diff * R1_R2 * R + V_diff * Correction = 0

    float a = v_diff - v_gpio;
    float b = v_diff * r1_r2;
    float c = v_diff * correction;

    // Quadratic formula: R = (-b ± sqrt(b^2 - 4ac)) / (2a)
    float discriminant = b * b - 4 * a * c;

    if (discriminant < 0)
        return -1.0f;  // No real solution

    float sqrt_discriminant = sqrt(discriminant);
    float r1 = (-b + sqrt_discriminant) / (2 * a);
    float r2 = (-b - sqrt_discriminant) / (2 * a);

    // Return the positive solution that makes physical sense
    // Since a < 0 (V_diff < V_gpio), we typically want the larger positive root
    if (r1 > 0 && r2 > 0) {
        return (r1 > r2) ? r1 : r2;  // Return larger positive value
    } else if (r1 > 0) {
        return r1;
    } else if (r2 > 0) {
        return r2;
    }

    return -1.0f;  // No valid solution
}

// Helper function for weighted slope optimization using relative errors
// Uses hybrid weighting to balance accuracy across the full resistance range
float EmpiricalResistorCalibrator::optimize_slope_weighted(float* R_values, float* V_diff_values, int num_points,
                                                           float v_gpio_open) {
    float best_r1_r2 = 100.0f;
    float best_error = 1e10f;

    // Sweep R1_R2 from 70 to 200 ohms
    for (float r1_r2_test = 70.0f; r1_r2_test <= 200.0f; r1_r2_test += 1.0f) {
        float weighted_error = 0.0f;
        float total_weight = 0.0f;

        for (int i = 0; i < num_points; i++) {
            // Calculate predicted voltage with current correction for this iteration
            float V_predicted = calculate_model_voltage(R_values[i], v_gpio_open, r1_r2_test, this->correction);

            // Calculate relative error (percentage error) for better accuracy metric
            float relative_error = (V_predicted - V_diff_values[i]) / V_diff_values[i];

            // Hybrid weighting: emphasize high R (where R1_R2 dominates) but don't ignore low R
            // weight = alpha*R + beta/R balances both regimes
            // For slope optimization, bias toward high R where R1_R2 has most effect
            float weight = R_values[i] + 0.5f / (R_values[i] + 0.1f);

            weighted_error += weight * relative_error * relative_error;
            total_weight += weight;
        }

        if (total_weight > 0) {
            weighted_error /= total_weight;
            if (weighted_error < best_error) {
                best_error = weighted_error;
                best_r1_r2 = r1_r2_test;
            }
        }
    }

    return best_r1_r2;
}

// Helper function for correction factor sweep optimization using relative errors
// Emphasizes low R values where correction term has maximum effect
float EmpiricalResistorCalibrator::optimize_correction_sweep(float* R_values, float* V_diff_values, int num_points,
                                                             float v_gpio_open, float r1_r2_fixed) {
    float best_correction = 0.0f;
    float best_error = 1e10f;

    // Sweep correction from -100 to 150
    for (float correction_test = -100.0f; correction_test <= 150.0f; correction_test += 1.0f) {
        float weighted_error = 0.0f;
        float total_weight = 0.0f;

        for (int i = 0; i < num_points; i++) {
            float V_predicted = calculate_model_voltage(R_values[i], v_gpio_open, r1_r2_fixed, correction_test);

            // Calculate relative error (percentage error) for better accuracy metric
            float relative_error = (V_predicted - V_diff_values[i]) / V_diff_values[i];

            // Strong emphasis on low R where Correction/R term dominates
            // Inverse weighting with small offset to avoid division by zero
            float weight = 1.0f / (R_values[i] + 0.1f);

            weighted_error += weight * relative_error * relative_error;
            total_weight += weight;
        }

        if (total_weight > 0) {
            weighted_error /= total_weight;
            if (weighted_error < best_error) {
                best_error = weighted_error;
                best_correction = correction_test;
            }
        }
    }

    return best_correction;
}

// Helper function to show calibration quality metrics
void EmpiricalResistorCalibrator::show_calibration_quality(float* R_values, float* V_diff_values, int num_points) {
    float rms_error = 0.0f;
    float max_error_abs = 0.0f;
    float max_error_percent = 0.0f;
    int good_count = 0, fair_count = 0, poor_count = 0;

    printf("Calibration Point Verification:\n");
    printf("R_actual   V_measured   V_model   Error_mV   Error_%%\n");
    printf("------------------------------------------------------\n");

    for (int i = 0; i < num_points; i++) {
        float V_predicted = calculate_model_voltage(R_values[i], this->v_gpio, this->r1_r2, this->correction);
        float error_mv = (V_predicted - V_diff_values[i]) * 1000.0f;
        float error_percent = fabs(error_mv) / (V_diff_values[i] * 1000.0f) * 100.0f;

        printf("%7.2f    %8.1f     %7.1f    %7.1f     %5.1f\n", R_values[i], V_diff_values[i] * 1000,
               V_predicted * 1000, error_mv, error_percent);

        rms_error += error_mv * error_mv;
        if (fabs(error_mv) > max_error_abs)
            max_error_abs = fabs(error_mv);
        if (error_percent > max_error_percent)
            max_error_percent = error_percent;

        if (error_percent < 2.0f)
            good_count++;
        else if (error_percent < 5.0f)
            fair_count++;
        else
            poor_count++;
    }

    rms_error = sqrt(rms_error / num_points);

    printf("------------------------------------------------------\n");
    printf("Quality Metrics:\n");
    printf("  RMS Error: %.1f mV\n", rms_error);
    printf("  Max Error: %.1f mV (%.1f%%)\n", max_error_abs, max_error_percent);
    printf("  Accuracy Distribution: %d excellent (<2%%), %d good (<5%%), %d poor (>5%%)\n", good_count, fair_count,
           poor_count);

    if (max_error_percent < 2.0f) {
        printf("  ✓ EXCELLENT calibration quality\n");
    } else if (max_error_percent < 5.0f) {
        printf("  ✓ GOOD calibration quality\n");
    } else if (max_error_percent < 10.0f) {
        printf("  ⚠ FAIR calibration quality - consider fine-tuning\n");
    } else {
        printf("  ✗ POOR calibration quality - needs improvement\n");
    }
}

// Helper function for interactive parameter tuning
void EmpiricalResistorCalibrator::interactive_parameter_tuning(float* R_values, float* V_diff_values, int num_points) {
    while (true) {
        printf("Current: R1_R2=%.1fΩ, Correction=%.1fΩ²\n", this->r1_r2, this->correction);
        printf("Commands: 'r' adjust R1_R2, 'c' adjust correction, 's' show results, 'q' finish: ");
        fflush(stdout);

        char cmd = read_char_from_uart();
        printf("%c\n", cmd);

        if (cmd == 'q' || cmd == 'Q') {
            break;
        } else if (cmd == 'r' || cmd == 'R') {
            printf("Enter new R1_R2 value (current=%.1f): ", this->r1_r2);
            fflush(stdout);
            float new_r1_r2 = read_float_from_uart();
            if (new_r1_r2 > 0) {
                this->r1_r2 = new_r1_r2;
                printf("R1_R2 updated to %.1f Ω\n", this->r1_r2);
            }
        } else if (cmd == 'c' || cmd == 'C') {
            printf("Enter new Correction value (current=%.1f): ", this->correction);
            fflush(stdout);
            float new_correction = read_float_from_uart();
            // if (new_correction >= 0) {
            this->correction = new_correction;
            printf("Correction updated to %.1f Ω²\n", this->correction);
            //}
        } else if (cmd == 's' || cmd == 'S') {
            show_calibration_quality(R_values, V_diff_values, num_points);
        }
        printf("\n");
    }
}

bool EmpiricalResistorCalibrator::least_squares_fit(float* R_values, float* V_diff_values, int num_points) {
    // Simple iterative optimization using gradient descent
    // Initial guess
    float v_gpio_est = 3.0f;       // Start with reasonable guess
    float r1_r2_est = 100.0f;      // Start with reasonable guess
    float correction_est = 30.0f;  // Start with reasonable guess

    const float learning_rate = 0.001f;
    const int max_iterations = 1000;
    const float tolerance = 1e-6f;

    for (int iter = 0; iter < max_iterations; iter++) {
        float total_error = 0.0f;
        float grad_v_gpio = 0.0f;
        float grad_r1_r2 = 0.0f;
        float grad_correction = 0.0f;

        // Calculate gradients
        for (int i = 0; i < num_points; i++) {
            float R = R_values[i];
            float V_measured = V_diff_values[i];
            float V_model = calculate_model_voltage(R, v_gpio_est, r1_r2_est, correction_est);
            float error = V_measured - V_model;
            total_error += error * error;

            // Partial derivatives (simplified numerical approximation)
            float denominator = R + r1_r2_est + correction_est / R;
            if (denominator > 0) {
                grad_v_gpio += -2 * error * R / denominator;
                grad_r1_r2 += -2 * error * (-v_gpio_est * R) / (denominator * denominator);
                grad_correction += -2 * error * (-v_gpio_est * R / R) / (denominator * denominator);
            }
        }

        // Update parameters
        v_gpio_est -= learning_rate * grad_v_gpio;
        r1_r2_est -= learning_rate * grad_r1_r2;
        correction_est -= learning_rate * grad_correction;

        // Clamp to reasonable ranges
        v_gpio_est = fmaxf(2.0f, fminf(4.0f, v_gpio_est));
        r1_r2_est = fmaxf(50.0f, fminf(200.0f, r1_r2_est));
        correction_est = fmaxf(1.0f, fminf(100.0f, correction_est));

        if (total_error < tolerance) {
            break;
        }
    }

    // Store results
    this->v_gpio = v_gpio_est;
    this->r1_r2 = r1_r2_est;
    this->correction = correction_est;

    printf("Empirical calibration results:\n");
    printf("  V_gpio = %.1f mV\n", v_gpio_est * 1000);
    printf("  R1_R2 = %.1f Ω\n", r1_r2_est);
    printf("  Correction = %.1f Ω²\n", correction_est);

    return true;
}

bool EmpiricalResistorCalibrator::calibrate_interactively_empirical() {
    printf("\n=== EMPIRICAL RESISTANCE CALIBRATOR ===\n");
    printf("This calibrator uses the empirical model:\n");
    printf("V_diff = V_gpio_open * R / (R + R1_R2 + Correction/R)\n");
    printf("Multi-stage calibration: Reference → Data → Slope → Correction → Fine-tune\n\n");

    // Step 0: Measure open circuit reference voltage
    printf("=== STEP 0: REFERENCE MEASUREMENT ===\n");
    printf("Disconnect ALL resistors from the circuit.\n");
    printf("Press ENTER when ready to measure open circuit voltage: ");
    fflush(stdout);

    if (read_char_from_uart(10) == 'q') {
        return false;
    }

    printf("Measuring open circuit voltage...\n");
    EmpiricalReading open_reading = read_differential_empirical(50);
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
    const int MAX_CALIBRATION_POINTS = 8;
    float R_values[MAX_CALIBRATION_POINTS];
    float V_diff_values[MAX_CALIBRATION_POINTS];
    int num_points = 0;

    printf("Now collect calibration data points with known resistors.\n");
    printf("Suggest: 1, 2, 3, 5, 8, 10, 12 ohms for good coverage.\n\n");
    printf("Know resistor must be connected between top and bottom 20mm sockets (mass)!\n\n");

    while (num_points < MAX_CALIBRATION_POINTS) {
        printf("[Point %d] Enter known resistance value (0 to finish, need minimum 4): ", num_points + 1);
        fflush(stdout);

        float R_known = read_float_from_uart();

        if (R_known <= 0) {
            if (num_points >= 4) {
                break;
            } else {
                printf("Need at least 4 calibration points for multi-stage fitting!\n");
                continue;
            }
        }

        printf("Connect %.2f Ω resistor and press ENTER...\n", R_known);
        wait_for_enter();

        // Measure differential voltage
        EmpiricalReading reading = read_differential_empirical(100);

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

    if (num_points < 4) {
        printf("Insufficient calibration points. Need at least 4.\n");
        return false;
    }

    printf("Collected %d calibration points.\n\n", num_points);

    // Step 2: Iterative optimization to handle parameter coupling
    printf("=== STEP 2: ITERATIVE PARAMETER OPTIMIZATION ===\n");
    printf("Using iterative refinement to find optimal parameters...\n\n");

    // Initialize with reasonable starting values
    this->v_gpio = v_gpio_open;
    this->r1_r2 = 100.0f;     // Initial guess
    this->correction = 0.0f;  // Start with no correction

    float best_r1_r2 = this->r1_r2;
    float best_correction = this->correction;

    // Iterate to refine both parameters (handles coupling between R1_R2 and Correction)
    const int max_iterations = 5;
    for (int iter = 0; iter < max_iterations; iter++) {
        printf("Iteration %d:\n", iter + 1);

        // Optimize R1_R2 with current correction value
        best_r1_r2 = optimize_slope_weighted(R_values, V_diff_values, num_points, v_gpio_open);
        this->r1_r2 = best_r1_r2;
        printf("  R1_R2 = %.1f Ω\n", best_r1_r2);

        // Optimize Correction with updated R1_R2 value
        best_correction = optimize_correction_sweep(R_values, V_diff_values, num_points, v_gpio_open, best_r1_r2);
        this->correction = best_correction;
        printf("  Correction = %.1f Ω²\n", best_correction);

        // Show iteration quality
        float rms_error = 0.0f;
        for (int i = 0; i < num_points; i++) {
            float V_predicted = calculate_model_voltage(R_values[i], v_gpio_open, best_r1_r2, best_correction);
            float error = (V_predicted - V_diff_values[i]) * 1000.0f;
            rms_error += error * error;
        }
        rms_error = sqrt(rms_error / num_points);
        printf("  RMS Error = %.2f mV\n\n", rms_error);
    }

    printf("Final optimized parameters:\n");
    printf("  R1_R2 = %.1f Ω\n", best_r1_r2);
    printf("  Correction = %.1f Ω²\n\n", best_correction);

    // Step 3: Show preliminary results with quality metrics
    printf("=== STEP 3: PRELIMINARY RESULTS ===\n");
    this->v_gpio = v_gpio_open;
    this->r1_r2 = best_r1_r2;
    this->correction = best_correction;

    show_calibration_quality(R_values, V_diff_values, num_points);

    // Step 4: Interactive fine-tuning
    printf("\n=== STEP 4: INTERACTIVE FINE-TUNING ===\n");
    printf("You can now manually adjust parameters for better fit.\n");
    printf("Commands: 'r' adjust R1_R2, 'c' adjust correction, 's' show results, 'q' finish\n\n");

    interactive_parameter_tuning(R_values, V_diff_values, num_points);

    // Final verification
    printf("\n=== CALIBRATION COMPLETE ===\n");
    show_calibration_quality(R_values, V_diff_values, num_points);

    printf("Final parameters:\n");
    printf("  V_gpio_open = %.1f mV\n", this->v_gpio * 1000);
    printf("  R1_R2 = %.1f Ω\n", this->r1_r2);
    printf("  Correction = %.1f Ω²\n", this->correction);

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
        EmpiricalReading test_reading = read_differential_empirical(100);

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

            if (error_percent < 5.0f) {
                printf("  ✓ GOOD: Error < 5%%\n");
            } else if (error_percent < 10.0f) {
                printf("  ⚠ FAIR: Error 5-10%%\n");
            } else {
                printf("  ✗ POOR: Error > 10%% - Consider recalibration\n");
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