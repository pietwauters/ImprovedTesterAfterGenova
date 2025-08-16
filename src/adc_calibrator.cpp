#include "adc_calibrator.h"

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

bool DifferentialResistorCalibrator::begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom) {
    channel_top = adc_channel_top;
    channel_bottom = adc_channel_bottom;

    // Configure ADC with ESP-IDF functions
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(channel_top, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(channel_bottom, ADC_ATTEN_DB_11);

    // Initialize calibration
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    printf("ADC initialized - channels %d and %d\n", channel_top, channel_bottom);

    // Test read both channels
    printf("Testing ADC reads...\n");
    for (int i = 0; i < 5; i++) {
        esp_task_wdt_reset();
        uint32_t raw_top = adc1_get_raw(channel_top);
        uint32_t raw_bottom = adc1_get_raw(channel_bottom);
        // printf("  Test %d: top=%lu, bottom=%lu\n", i, raw_top, raw_bottom);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return load_calibration_from_nvs();
}

float DifferentialResistorCalibrator::read_voltage_single(adc1_channel_t channel) {
    uint32_t raw = adc1_get_raw(channel);
    return esp_adc_cal_raw_to_voltage(raw, &adc_chars) / 1000.0f;
}

float DifferentialResistorCalibrator::read_voltage_average(adc1_channel_t channel, int samples) {
    uint32_t total = 0;

    printf("Reading %d samples from channel %d...\n", samples, channel);

    for (int i = 0; i < samples; ++i) {
        esp_task_wdt_reset();  // Reset WDT every sample

        uint32_t raw = adc1_get_raw(channel);
        total += raw;

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t avg_raw = total / samples;
    printf("Average raw value: %lu\n", avg_raw);

    return esp_adc_cal_raw_to_voltage(avg_raw, &adc_chars) / 1000.0f;
}

DifferentialResistorCalibrator::DifferentialReading DifferentialResistorCalibrator::read_differential(int samples) {
    DifferentialReading result;

    printf("Starting differential read with %d samples...\n", samples);

    // Take samples from both channels and convert to millivolts using eFuse calibration
    uint64_t total_mv_top = 0, total_mv_bottom = 0;

    for (int i = 0; i < samples; ++i) {
        esp_task_wdt_reset();  // Reset WDT every iteration

        uint32_t raw_top = adc1_get_raw(channel_top);
        uint32_t raw_bottom = adc1_get_raw(channel_bottom);

        // Convert each raw sample to millivolts using eFuse calibration
        uint32_t mv_top = esp_adc_cal_raw_to_voltage(raw_top, &adc_chars);
        uint32_t mv_bottom = esp_adc_cal_raw_to_voltage(raw_bottom, &adc_chars);

        total_mv_top += mv_top;
        total_mv_bottom += mv_bottom;

        if (i < samples - 1)
            vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Calculate average of calibrated millivolt values
    uint32_t avg_mv_top = total_mv_top / samples;
    uint32_t avg_mv_bottom = total_mv_bottom / samples;

    // Convert millivolts to volts
    result.v_top = avg_mv_top / 1000.0f;
    result.v_bottom = avg_mv_bottom / 1000.0f;
    result.v_diff = result.v_top - result.v_bottom;

    printf("Differential result: v_top=%.3f V, v_bottom=%.3f V, v_diff=%.3f V\n", result.v_top, result.v_bottom,
           result.v_diff);

    // Calculate resistance
    result.resistance = get_resistance_from_differential(result.v_top, result.v_bottom);

    return result;
}

DifferentialResistorCalibrator::DifferentialReading DifferentialResistorCalibrator::read_differential_average(
    int samples) {
    return read_differential(samples);
}

DifferentialResistorCalibrator::DifferentialReading DifferentialResistorCalibrator::read_differential_median(
    int samples) {
    DifferentialReading result;

    printf("Starting differential read with %d samples (median filtering)...\n", samples);

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

    // Sort arrays to find median (sorting millivolt values now)
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

    // Get median values from calibrated millivolt arrays
    uint32_t median_mv_top, median_mv_bottom;
    if (samples % 2 == 0) {
        // Even number of samples - average the two middle values
        median_mv_top = (mv_top_samples[samples / 2 - 1] + mv_top_samples[samples / 2]) / 2;
        median_mv_bottom = (mv_bottom_samples[samples / 2 - 1] + mv_bottom_samples[samples / 2]) / 2;
    } else {
        // Odd number of samples - take the middle value
        median_mv_top = mv_top_samples[samples / 2];
        median_mv_bottom = mv_bottom_samples[samples / 2];
    }

    // Clean up arrays
    delete[] mv_top_samples;
    delete[] mv_bottom_samples;

    // Convert millivolts to volts
    result.v_top = median_mv_top / 1000.0f;
    result.v_bottom = median_mv_bottom / 1000.0f;
    result.v_diff = result.v_top - result.v_bottom;

    printf("Differential result (median): v_top=%.3f V, v_bottom=%.3f V, v_diff=%.3f V\n", result.v_top,
           result.v_bottom, result.v_diff);

    // Calculate resistance
    result.resistance = get_resistance_from_differential(result.v_top, result.v_bottom);

    return result;
}

DifferentialResistorCalibrator::DifferentialReading DifferentialResistorCalibrator::read_differential_trimmed_mean(
    int samples, float trim_percent) {
    DifferentialReading result;

    printf("Starting differential read with %d samples (trimmed mean, removing %.0f%% outliers)...\n", samples,
           trim_percent * 100);

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

    printf("Differential result (trimmed mean): v_top=%.3f V, v_bottom=%.3f V, v_diff=%.3f V\n", result.v_top,
           result.v_bottom, result.v_diff);
    printf("  Used %d samples (removed %d outliers from each end)\n", valid_samples, trim_count);

    // Calculate resistance
    result.resistance = get_resistance_from_differential(result.v_top, result.v_bottom);

    return result;
}

float DifferentialResistorCalibrator::get_resistance_from_differential(float v_top, float v_bottom) {
    if (r1_eff <= 0 || r3_eff <= 0 || v_top <= v_bottom || v_bottom <= 0) {
        return -1.0f;  // Invalid
    }

    float v_diff = v_top - v_bottom;
    if (v_diff <= 0)
        return -1.0f;

    // For your 0-15Ω range with 47Ω fixed resistors:
    // From voltage divider: v_bottom = v_gpio * r3_eff / (r1_eff + R + r3_eff)
    // From differential: v_diff = v_gpio * R / (r1_eff + R + r3_eff)
    // Therefore: R = r3_eff * v_diff / v_bottom
    float R_raw = r3_eff * v_diff / v_bottom;

    // Apply empirical slope correction (based on measurement data: 1.2Ω OK, 8.4Ω→8.7Ω)
    // Linear correction factor to compensate for systematic slope error
    float slope_correction = 0.966f;  // Tune this based on your calibration measurements
    return R_raw * slope_correction;
}

bool DifferentialResistorCalibrator::calibrate_interactively(float R_known) {
    printf("\n=== Differential Resistor Calibrator (47Ω Fixed Resistors) ===\n");
    printf("Target measurement range: 0-15Ω\n");

    printf("[Step 1] Connect known resistor (%.1f Ω), then press ENTER...\n", R_known);
    printf("Note: Use a resistor in your 0-15Ω range for best accuracy.\n");
    wait_for_enter();

    // Measure with known resistor using trimmed mean filtering for better accuracy
    DifferentialReading known_reading = read_differential_trimmed_mean(1000, 0.2f);

    printf("Known Resistor Measurements:\n");
    printf("  V_TOP = %.3f V\n", known_reading.v_top);
    printf("  V_BOTTOM = %.3f V\n", known_reading.v_bottom);
    printf("  V_DIFF = %.3f V\n", known_reading.v_diff);

    if (known_reading.v_diff <= 0.0f || known_reading.v_bottom <= 0.0f) {
        printf("Invalid voltage readings for known resistor. Calibration failed.\n");
        return false;
    }

    // Calculate current through known resistor: I = V_diff / R_known
    float I_known = known_reading.v_diff / R_known;
    printf("\nCalculated current: %.6f A\n", I_known);

    // Calculate R3 (bottom branch): R3 * I = V_bottom
    r3_eff = known_reading.v_bottom / I_known;

    // Calculate Vgpio and R1 from voltage divider equations
    // V_bottom = V_gpio * R3 / (R1 + R_known + R3)
    // V_top = V_gpio * (R_known + R3) / (R1 + R_known + R3)
    // Therefore: V_gpio = V_top + V_diff * R1 / R_known
    // But we need R1 first...

    // Verify this calculation using voltage divider equations:
    // V_top / V_bottom = (R_known + R3) / R3
    // This gives us: R_known = R3 * (V_top / V_bottom - 1)
    float R_verify = r3_eff * (known_reading.v_top / known_reading.v_bottom - 1.0f);
    printf("Verification: R3=%.2f Ω, R_verify=%.2f Ω (should be %.1f Ω)\n", r3_eff, R_verify, R_known);

    // Calculate V_gpio from: V_gpio = V_bottom * (R1 + R_known + R3) / R3
    // But we need R1. From current balance: I = (V_gpio - V_top) / R1
    // So: R1 = (V_gpio - V_top) / I
    // Substituting: V_gpio = V_top + I * R1
    // From voltage divider: V_bottom * (R1 + R_known + R3) = V_gpio * R3
    // Therefore: V_gpio = V_bottom * (R1 + R_known + R3) / R3
    // Solving: V_gpio = (V_top * R3 + I_known * R_known * R3) / (R3 - I_known * R3)

    // Simpler approach: V_gpio = V_bottom / V_bottom * V_gpio = V_bottom * (total_resistance) / R3
    // Where total_resistance can be found from the current: I = V_diff / R_known = V_gpio * R_known / total_resistance
    // So: total_resistance = V_gpio * R_known / V_diff
    // But V_gpio = V_bottom * total_resistance / R3
    // Substituting: total_resistance = V_bottom * total_resistance * R_known / (R3 * V_diff)
    // Solving: 1 = V_bottom * R_known / (R3 * V_diff)
    // This gives: R3 = V_bottom * R_known / V_diff (which matches what we calculated above)

    // Calculate V_gpio and R1 using correct voltage divider equations:
    // V_bottom = V_gpio * R3 / (R1 + R_known + R3)
    // V_top = V_gpio * (R_known + R3) / (R1 + R_known + R3)
    // From these: V_top / V_bottom = (R_known + R3) / R3
    // We already verified this gives the correct R_known

    // CORRECT calculation using voltage divider:
    // V_bottom = V_gpio * R3 / (R1 + R_known + R3)
    // V_top = V_gpio * (R_known + R3) / (R1 + R_known + R3)
    // Therefore: V_top / V_bottom = (R_known + R3) / R3
    // This gives: R_known + R3 = R3 * V_top / V_bottom
    // So: R_known = R3 * (V_top / V_bottom - 1) = R3 * V_diff / V_bottom
    // Since R3 = V_bottom / I: R_known = (V_bottom / I) * (V_diff / V_bottom) = V_diff / I ✓

    // Now solve for V_gpio and R1:
    // From V_bottom = V_gpio * R3 / (R1 + R_known + R3):
    // V_gpio = V_bottom * (R1 + R_known + R3) / R3
    // From V_top = V_gpio * (R_known + R3) / (R1 + R_known + R3):
    // V_gpio = V_top * (R1 + R_known + R3) / (R_known + R3)

    // Equating these: V_bottom * (R1 + R_known + R3) / R3 = V_top * (R1 + R_known + R3) / (R_known + R3)
    // Simplifying: V_bottom / R3 = V_top / (R_known + R3)
    // Cross multiply: V_bottom * (R_known + R3) = V_top * R3
    // This gives: V_bottom * R_known + V_bottom * R3 = V_top * R3
    // So: V_bottom * R_known = R3 * (V_top - V_bottom) = R3 * V_diff
    // Therefore: R_known = R3 * V_diff / V_bottom ✓ (matches our earlier calculation)

    // To find V_gpio: Use V_gpio = V_top / (voltage divider ratio for top)
    // Voltage divider ratio = (R_known + R3) / (R1 + R_known + R3)
    // From V_bottom equation: (R1 + R_known + R3) = V_gpio * R3 / V_bottom
    // So: V_gpio = V_bottom * (R1 + R_known + R3) / R3
    // We need R1. From the current: I = V_gpio / (R1 + R_known + R3)
    // And: I = V_diff / R_known
    // So: V_diff / R_known = V_gpio / (R1 + R_known + R3)
    // Therefore: (R1 + R_known + R3) = V_gpio * R_known / V_diff
    // Substituting into V_gpio equation: V_gpio = V_bottom * (V_gpio * R_known / V_diff) / R3
    // Solving: V_diff * R3 = V_bottom * R_known ✓ (confirmed)
    // And: V_gpio = V_diff * (R1 + R_known + R3) / R_known

    // Since (R1 + R_known + R3) = V_gpio / I and I = V_diff / R_known:
    // (R1 + R_known + R3) = V_gpio * R_known / V_diff
    // Now solve for V_gpio and R1 using the corrected voltage divider equations:
    // We already have v_gpio_low and r3_eff from the measurements above

    // From corrected voltage divider for short circuit:
    // v_bottom_short = v_gpio_low + (v_gpio - v_gpio_low) * r3_eff / (r1_eff + r3_eff)
    // Rearranging: (v_gpio - v_gpio_low) = (v_bottom_short - v_gpio_low) * (r1_eff + r3_eff) / r3_eff
    // So: v_gpio = v_gpio_low + (v_bottom_short - v_gpio_low) * (r1_eff + r3_eff) / r3_eff

    // From corrected voltage divider for known resistor:
    // v_bottom_known = v_gpio_low + (v_gpio - v_gpio_low) * r3_eff / (r1_eff + R_known + r3_eff)
    // Rearranging: (v_gpio - v_gpio_low) = (v_bottom_known - v_gpio_low) * (r1_eff + R_known + r3_eff) / r3_eff

    // Equating the two expressions for (v_gpio - v_gpio_low):
    // (v_bottom_short - v_gpio_low) * (r1_eff + r3_eff) / r3_eff = (v_bottom_known - v_gpio_low) * (r1_eff + R_known +
    // r3_eff) / r3_eff Simplifying: (v_bottom_short - v_gpio_low) * (r1_eff + r3_eff) = (v_bottom_known - v_gpio_low) *
    // (r1_eff + R_known + r3_eff) Expanding: (v_bottom_short - v_gpio_low) * r1_eff + (v_bottom_short - v_gpio_low) *
    // r3_eff = (v_bottom_known - v_gpio_low) * r1_eff + (v_bottom_known - v_gpio_low) * (R_known + r3_eff) Collecting
    // r1_eff terms: r1_eff * [(v_bottom_short - v_gpio_low) - (v_bottom_known - v_gpio_low)] = (v_bottom_known -
    // v_gpio_low) * (R_known + r3_eff) - (v_bottom_short - v_gpio_low) * r3_eff Therefore: r1_eff = [(v_bottom_known -
    // v_gpio_low) * (R_known + r3_eff) - (v_bottom_short - v_gpio_low) * r3_eff] / [(v_bottom_short - v_gpio_low) -
    // (v_bottom_known - v_gpio_low)]

    // Use short circuit measurement to solve for R1 and V_gpio
    printf("\n[Step 2] Short circuit measurement - Short the test pins together, then press ENTER...\n");
    wait_for_enter();

    // Measure with short circuit using trimmed mean filtering for better accuracy
    DifferentialReading short_reading = read_differential_trimmed_mean(500, 0.2f);

    printf("Short circuit readings: V_top=%.3fV, V_bottom=%.3fV\n", short_reading.v_top, short_reading.v_bottom);

    // Solve for R1 using the original method:
    float A = known_reading.v_top * this->r3_eff;
    float B = short_reading.v_bottom * (R_known + this->r3_eff);

    this->r1_eff = (B * this->r3_eff - A * (R_known + this->r3_eff)) / (A - B);

    // Calculate V_gpio using short circuit equation:
    this->v_gpio = short_reading.v_bottom * (this->r1_eff + this->r3_eff) / this->r3_eff;

    printf("\nCalculated Parameters (Two-Step Method):\n");
    printf("  Step 1: R3_eff = %.2f Ω (from known resistor measurement)\n", this->r3_eff);
    printf("  Step 2: R1_eff = %.2f Ω (solved from both measurements)\n", this->r1_eff);
    printf("  Step 2: V_gpio = %.3f V (calculated from circuit equations)\n", this->v_gpio);

    // Calculate and display currents for both steps
    float I_step1 = this->v_gpio / (this->r1_eff + R_known + this->r3_eff);
    float I_step2 = this->v_gpio / (this->r1_eff + this->r3_eff);
    printf("  Step 1 current (with %.1fΩ): %.6f A\n", R_known, I_step1);
    printf("  Step 2 current (short circuit): %.6f A\n", I_step2);
    printf("  Total resistance step 1 (R1+R_known+R3) = %.2f Ω\n", this->r1_eff + R_known + this->r3_eff);
    printf("  Total resistance step 2 (R1+R3) = %.2f Ω\n", this->r1_eff + this->r3_eff);

    // Verification: compare calculated vs measured currents
    printf("\nVerification:\n");
    printf("  Step 1 measured current: %.6f A (calculated: %.6f A)\n", I_known, I_step1);

    // Show the voltage drops
    float voltage_drop_known = this->v_gpio - known_reading.v_top;
    printf("  Voltage drop with %.1fΩ load: %.3f V (%.1f%%)\n", R_known, voltage_drop_known,
           voltage_drop_known / this->v_gpio * 100);

    // Sanity checks
    if (this->r1_eff < 0 || this->r3_eff < 0) {
        printf("WARNING: Effective resistances are negative. Check connections and known resistor value.\n");
    }

    // Verify measurement
    float R_verify_known = get_resistance_from_differential(known_reading.v_top, known_reading.v_bottom);

    printf("\nVerification:\n");
    printf("  Known resistor: Calculated R = %.2f Ω (should be %.1f Ω)\n", R_verify_known, R_known);

    float error_known = fabs(R_verify_known - R_known) / R_known * 100.0f;
    printf("  Calibration error: %.1f%%\n", error_known);

    // Interactive verification
    printf("\n[Step 3] Test different resistors in 0-15Ω range (press 'q' to quit).\n");
    while (true) {
        char key = wait_for_key();
        if (key == 'q')
            break;

        DifferentialReading test_reading = read_differential_trimmed_mean(36, 0.2f);

        if (test_reading.v_bottom <= 0) {
            printf("Invalid reading. Check connections.\n");
            continue;
        }

        printf("Measurements: V_top=%.3f V, V_bottom=%.3f V, V_diff=%.3f V\n", test_reading.v_top,
               test_reading.v_bottom, test_reading.v_diff);
        printf("Calculated Resistance: %.1f Ω\n", test_reading.resistance);

        if (test_reading.resistance > 15.0f) {
            printf("WARNING: Resistance above 15Ω target range.\n");
        }

        int threshold = get_adc_threshold_for_resistance(test_reading.resistance);
        printf("ADC threshold for this resistance: %d\n", threshold);
    }

    return true;
}

int DifferentialResistorCalibrator::get_adc_threshold_for_resistance(float R) {
    if (r1_eff <= 0 || r3_eff <= 0)
        return -1;

    // Calculate expected bottom voltage for this resistance
    float v_bottom_expected = v_gpio * r3_eff / (r1_eff + R + r3_eff);

    // Convert to ADC raw value
    return voltage_to_adc_raw(v_bottom_expected);
}

int DifferentialResistorCalibrator::voltage_to_adc_raw(float voltage) {
    // Binary search to find ADC value that gives closest voltage
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

bool DifferentialResistorCalibrator::save_calibration_to_nvs(const char* nvs_namespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return false;

    err |= nvs_set_blob(handle, "v_gpio", &v_gpio, sizeof(float));
    err |= nvs_set_blob(handle, "r1_eff", &r1_eff, sizeof(float));
    err |= nvs_set_blob(handle, "r3_eff", &r3_eff, sizeof(float));
    err |= nvs_set_blob(handle, "Version", &Version, sizeof(int));
    err |= nvs_commit(handle);

    nvs_close(handle);
    return err == ESP_OK;
}

bool DifferentialResistorCalibrator::load_calibration_from_nvs(const char* nvs_namespace) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return false;

    size_t required_size = sizeof(float);
    size_t required_int_size = sizeof(int);
    err |= nvs_get_blob(handle, "v_gpio", &v_gpio, &required_size);
    err |= nvs_get_blob(handle, "r1_eff", &r1_eff, &required_size);
    err |= nvs_get_blob(handle, "r3_eff", &r3_eff, &required_size);
    err |= nvs_get_blob(handle, "Version", &Version, &required_int_size);
    nvs_close(handle);
    if (Version < CurrentVersion)
        return false;
    return err == ESP_OK;
}

void DifferentialResistorCalibrator::wait_for_enter() {
    printf("Press ENTER to continue...\n");
    uart_flush_input(UART_NUM_0);
    uint8_t c;
    while (true) {
        int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(10));
        if (len > 0 && (c == '\n' || c == '\r'))
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
    }
}

char DifferentialResistorCalibrator::wait_for_key() {
    printf("Press a key (q to quit, ENTER to continue): ");
    uart_flush_input(UART_NUM_0);
    uint8_t c;
    while (true) {
        int len = uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(10));
        if (len > 0) {
            if (c == '\n' || c == '\r')
                return '\n';
            return (char)c;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
    }
}

// ========================================================================
// EMPIRICAL RESISTOR CALIBRATOR IMPLEMENTATION
// ========================================================================

bool EmpiricalResistorCalibrator::begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom) {
    this->channel_top = adc_channel_top;
    this->channel_bottom = adc_channel_bottom;

    printf("Empirical calibrator: Configuring ADC channels top=%d, bottom=%d\n", channel_top, channel_bottom);

    // Configure ADC - same as differential calibrator
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(this->channel_top, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(this->channel_bottom, ADC_ATTEN_DB_11);

    // Initialize calibration - same as differential calibrator
    esp_adc_cal_value_t cal_type =
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    printf("ADC initialized - channels %d and %d\n", channel_top, channel_bottom);
    printf("ADC calibration type: %d, vref: %d mV, coeff_a: %d, coeff_b: %d\n", cal_type, adc_chars.vref,
           adc_chars.coeff_a, adc_chars.coeff_b);

    // Test the calibration function directly
    printf("Testing esp_adc_cal_raw_to_voltage():\n");
    for (uint32_t test_raw = 100; test_raw <= 1000; test_raw += 100) {
        uint32_t result_mv = esp_adc_cal_raw_to_voltage(test_raw, &adc_chars);
        printf("  Raw %lu -> %lu mV\n", test_raw, result_mv);
    }

    return true;
}

EmpiricalResistorCalibrator::EmpiricalReading EmpiricalResistorCalibrator::read_differential_empirical(int samples) {
    EmpiricalReading result;

    printf("Starting empirical differential read with %d samples (trimmed mean filtering)...\n", samples);

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

        // DEBUG: Show first few samples to diagnose issues
        if (i < 5) {
            printf("  Sample %d: raw_top=%lu→%lumV, raw_bottom=%lu→%lumV, diff=%ldmV\n", i, raw_top, mv_top_samples[i],
                   raw_bottom, mv_bottom_samples[i], (long)(mv_top_samples[i] - mv_bottom_samples[i]));
        }

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

float EmpiricalResistorCalibrator::get_resistance_empirical(float v_diff_measured) {
    if (v_gpio <= 0 || r1_r2 <= 0) {
        return -1.0f;  // Not calibrated
    }

    if (v_diff_measured <= 0 || v_diff_measured >= v_gpio) {
        return -1.0f;  // Invalid measurement
    }

    // Solve empirical model: V_diff_measured = V_gpio * R / (R + R1_R2 + Correction/R)
    // This is a quadratic equation in R. Rearranging:
    // V_diff_measured * (R + R1_R2 + Correction/R) = V_gpio * R
    // V_diff_measured * R + V_diff_measured * R1_R2 + V_diff_measured * Correction/R = V_gpio * R
    // V_diff_measured * R1_R2 + V_diff_measured * Correction/R = R * (V_gpio - V_diff_measured)
    // Multiply by R: V_diff_measured * R1_R2 * R + V_diff_measured * Correction = R^2 * (V_gpio - V_diff_measured)
    // Rearrange: R^2 * (V_gpio - V_diff_measured) - V_diff_measured * R1_R2 * R - V_diff_measured * Correction = 0
    // Standard form: a*R^2 + b*R + c = 0

    float a = v_gpio - v_diff_measured;
    float b = -v_diff_measured * r1_r2;
    float c = -v_diff_measured * correction;

    if (fabs(a) < 1e-9) {
        return -1.0f;  // Degenerate case
    }

    // Quadratic formula: R = (-b ± sqrt(b^2 - 4ac)) / (2a)
    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        return -1.0f;  // No real solution
    }

    float sqrt_discriminant = sqrtf(discriminant);
    float r1 = (-b + sqrt_discriminant) / (2 * a);
    float r2 = (-b - sqrt_discriminant) / (2 * a);

    // Choose the positive solution
    if (r1 > 0 && r2 > 0) {
        return fminf(r1, r2);  // Choose smaller positive root
    } else if (r1 > 0) {
        return r1;
    } else if (r2 > 0) {
        return r2;
    } else {
        return -1.0f;  // No positive solution
    }
}

float EmpiricalResistorCalibrator::calculate_model_voltage(float R_known, float v_gpio, float r1_r2, float correction) {
    if (R_known <= 0)
        return 0.0f;
    return v_gpio * R_known / (R_known + r1_r2 + correction / R_known);
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
    printf("\n=== EMPIRICAL RESISTOR CALIBRATOR ===\n");
    printf("Model: V_diff = V_gpio * R / (R + R1_R2 + Correction/R)\n");
    printf("Where V_diff is the voltage ACROSS the unknown resistor (what you measure).\n");
    printf("This calibration requires 4-6 known resistors for best accuracy.\n\n");

    const int MAX_CALIBRATION_POINTS = 8;
    float R_values[MAX_CALIBRATION_POINTS];
    float V_diff_values[MAX_CALIBRATION_POINTS];
    int num_points = 0;

    while (num_points < MAX_CALIBRATION_POINTS) {
        printf("[Point %d] Enter known resistance value (0 to finish, need minimum 3): ", num_points + 1);
        fflush(stdout);  // Force output buffer to display immediately

        // ESP32-safe input reading with WDT reset
        float R_known = read_float_from_uart();

        if (R_known <= 0) {
            if (num_points >= 3) {
                break;
            } else {
                printf("Need at least 3 calibration points!\n");
                continue;
            }
        }

        printf("Connect %.2f Ω resistor and press ENTER...\n", R_known);
        wait_for_enter();

        // Measure differential voltage
        EmpiricalReading reading = read_differential_empirical(1000);

        printf("Measured: R=%.2fΩ → V_diff=%.1fmV (V_top=%.1fmV, V_bottom=%.1fmV)\n", R_known, reading.v_diff * 1000,
               reading.v_top * 1000, reading.v_bottom * 1000);

        R_values[num_points] = R_known;
        V_diff_values[num_points] = reading.v_diff;  // Use v_diff (voltage across resistor) for empirical model
        num_points++;

        printf("Point %d recorded. Continue? (y/n): ", num_points);
        fflush(stdout);
        char response = read_char_from_uart();
        if (response != 'y' && response != 'Y' && num_points >= 3) {
            break;
        }
    }

    printf("\nPerforming least squares calibration with %d points...\n", num_points);

    bool success = least_squares_fit(R_values, V_diff_values, num_points);

    if (success) {
        printf("\nCalibration successful!\n");
        printf("Verify with test measurements:\n");

        for (int i = 0; i < num_points; i++) {
            float predicted = calculate_model_voltage(R_values[i], v_gpio, r1_r2, correction);
            float error = (predicted - V_diff_values[i]) * 1000;  // mV
            printf("  R=%.2fΩ: Measured=%.1fmV, Model=%.1fmV, Error=%.1fmV\n", R_values[i], V_diff_values[i] * 1000,
                   predicted * 1000, error);
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
            EmpiricalReading test_reading = read_differential_empirical(300);

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
    }

    return success;
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

char EmpiricalResistorCalibrator::read_char_from_uart() {
    // Clear input buffer
    uart_flush_input(UART_NUM_0);

    // Show cursor prompt
    printf(">> ");
    fflush(stdout);

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