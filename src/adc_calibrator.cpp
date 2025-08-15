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
        printf("  Test %d: top=%lu, bottom=%lu\n", i, raw_top, raw_bottom);
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

        // Print progress every 100 samples
        if (i % 100 == 0) {
            printf("  Sample %d/%d, raw=%lu\n", i, samples, raw);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint32_t avg_raw = total / samples;
    printf("Average raw value: %lu\n", avg_raw);

    return esp_adc_cal_raw_to_voltage(avg_raw, &adc_chars) / 1000.0f;
}

DifferentialResistorCalibrator::DifferentialReading DifferentialResistorCalibrator::read_differential(int samples) {
    DifferentialReading result;

    printf("Starting differential read with %d samples...\n", samples);

    // Take samples from both channels
    uint32_t total_top = 0, total_bottom = 0;

    for (int i = 0; i < samples; ++i) {
        esp_task_wdt_reset();  // Reset WDT every iteration

        uint32_t raw_top = adc1_get_raw(channel_top);
        uint32_t raw_bottom = adc1_get_raw(channel_bottom);

        total_top += raw_top;
        total_bottom += raw_bottom;

        if (i % 4 == 0) {  // Print every 4th sample to avoid spam
            printf("  Sample %d: top=%lu, bottom=%lu\n", i, raw_top, raw_bottom);
        }

        if (i < samples - 1)
            vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Convert to voltages
    uint32_t avg_raw_top = total_top / samples;
    uint32_t avg_raw_bottom = total_bottom / samples;

    result.v_top = esp_adc_cal_raw_to_voltage(avg_raw_top, &adc_chars) / 1000.0f;
    result.v_bottom = esp_adc_cal_raw_to_voltage(avg_raw_bottom, &adc_chars) / 1000.0f;
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
    return r3_eff * v_diff / v_bottom;
}

bool DifferentialResistorCalibrator::calibrate_interactively(float R_known) {
    printf("\n=== Differential Resistor Calibrator (47Ω Fixed Resistors) ===\n");
    printf("Target measurement range: 0-15Ω\n");

    printf("[Step 1] Connect known resistor (%.1f Ω), then press ENTER...\n", R_known);
    printf("Note: Use a resistor in your 0-15Ω range for best accuracy.\n");
    wait_for_enter();

    // Measure with known resistor
    DifferentialReading known_reading = read_differential_average(1000);

    printf("Known Resistor Measurements:\n");
    printf("  V_TOP = %.3f V\n", known_reading.v_top);
    printf("  V_BOTTOM = %.3f V\n", known_reading.v_bottom);
    printf("  V_DIFF = %.3f V\n", known_reading.v_diff);

    if (known_reading.v_diff <= 0.0f || known_reading.v_bottom <= 0.0f) {
        printf("Invalid voltage readings for known resistor. Calibration failed.\n");
        return false;
    }

    printf("\n[Step 2] Create short circuit (connect wire/jumper), then press ENTER...\n");
    printf("This creates R = 0Ω for the second calibration point.\n");
    wait_for_enter();

    // Measure with short circuit (R = 0)
    DifferentialReading short_reading = read_differential_average(1000);

    printf("Short Circuit Measurements:\n");
    printf("  V_TOP = %.3f V\n", short_reading.v_top);
    printf("  V_BOTTOM = %.3f V\n", short_reading.v_bottom);
    printf("  V_DIFF = %.3f V (should be ~0V)\n", short_reading.v_diff);

    if (short_reading.v_bottom <= 0.0f) {
        printf("Invalid voltage readings for short circuit. Calibration failed.\n");
        return false;
    }

    // Now we have 4 equations for 3 unknowns:
    // From known resistor:
    // 1) v_top_known = v_gpio * (R_known + r3_eff) / (r1_eff + R_known + r3_eff)
    // 2) v_bottom_known = v_gpio * r3_eff / (r1_eff + R_known + r3_eff)
    //
    // From short circuit (R = 0):
    // 3) v_top_short = v_gpio * r3_eff / (r1_eff + r3_eff)
    // 4) v_bottom_short = v_gpio * r3_eff / (r1_eff + r3_eff)
    //
    // Note: equations 3 and 4 should be identical (v_diff = 0)

    // Check that short circuit gives nearly zero differential
    if (fabs(short_reading.v_diff) > 0.050f) {  // 50mV tolerance
        printf("WARNING: Short circuit differential is %.3f V (expected ~0V)\n", short_reading.v_diff);
        printf("         Check short circuit connection.\n");
    }

    // Use average of v_top and v_bottom for short circuit (they should be equal)
    float v_short = (short_reading.v_top + short_reading.v_bottom) / 2.0f;

    printf("Using short circuit voltage: %.3f V (average of top/bottom)\n", v_short);

    // Solve the system of equations:
    // From short circuit: v_short = v_gpio * r3_eff / (r1_eff + r3_eff)
    // From known resistor ratio: v_bottom_known / v_short = (r1_eff + r3_eff) / (r1_eff + R_known + r3_eff)

    // Let's define: total_short = r1_eff + r3_eff
    // From short: r3_eff = v_short * total_short / v_gpio
    // From known: (r1_eff + R_known + r3_eff) = (r1_eff + r3_eff) * v_short / v_bottom_known
    //            = total_short * v_short / v_bottom_known

    // Therefore: R_known = total_short * v_short / v_bottom_known - total_short
    //           total_short = R_known / (v_short / v_bottom_known - 1)

    float voltage_ratio = v_short / known_reading.v_bottom;
    if (voltage_ratio <= 1.0f) {
        printf("ERROR: Invalid voltage ratio (%.3f). Short circuit voltage should be higher than loaded voltage.\n",
               voltage_ratio);
        return false;
    }

    float total_short = R_known / (voltage_ratio - 1.0f);

    // Now we can find v_gpio from the differential measurement
    // v_diff_known = v_gpio * R_known / (r1_eff + R_known + r3_eff)
    // v_bottom_known = v_gpio * r3_eff / (r1_eff + R_known + r3_eff)
    // Therefore: v_gpio = v_bottom_known * (total_short + R_known) / r3_eff

    // But we need r3_eff first. From the known resistor:
    // r3_eff / (total_short + R_known) = v_bottom_known / v_gpio
    // And from short circuit: r3_eff / total_short = v_short / v_gpio
    // Therefore: r3_eff = total_short * v_short / v_short = total_short * known_reading.v_bottom / v_short

    r3_eff = total_short * known_reading.v_bottom / v_short;
    r1_eff = total_short - r3_eff;
    v_gpio = v_short * total_short / r3_eff;

    printf("\nCalculated Parameters:\n");
    printf("  v_gpio = %.3f V\n", v_gpio);
    printf("  R1_eff = %.2f Ω (47Ω + GPIO Ron + parasitics)\n", r1_eff);
    printf("  R3_eff = %.2f Ω (47Ω + GPIO Ron + parasitics)\n", r3_eff);
    printf("  Total resistance (R1+R3) = %.2f Ω\n", total_short);

    // Show the voltage drops
    float voltage_drop_known = v_gpio - known_reading.v_top;
    float voltage_drop_short = v_gpio - v_short;
    printf("  Voltage drop with %.1fΩ load: %.3f V (%.1f%%)\n", R_known, voltage_drop_known,
           voltage_drop_known / v_gpio * 100);
    printf("  Voltage drop with short circuit: %.3f V (%.1f%%)\n", voltage_drop_short,
           voltage_drop_short / v_gpio * 100);

    // Sanity checks
    if (r1_eff < 60 || r1_eff > 100 || r3_eff < 60 || r3_eff > 100) {
        printf("WARNING: Effective resistances are outside expected range (60-100Ω).\n");
        printf("         Expected: 47Ω (resistor) + ~25-40Ω (GPIO Ron) + parasitics\n");
        printf("         Check connections and known resistor value.\n");
    } else {
        printf("  Effective resistances are within expected range.\n");
        printf("  GPIO Ron contribution: R1=%.1fΩ, R3=%.1fΩ\n", r1_eff - 47.0f, r3_eff - 47.0f);
    }

    // Verify both measurements
    float R_verify_known = get_resistance_from_differential(known_reading.v_top, known_reading.v_bottom);
    float R_verify_short = get_resistance_from_differential(short_reading.v_top, short_reading.v_bottom);

    printf("\nVerification:\n");
    printf("  Known resistor: Calculated R = %.2f Ω (should be %.1f Ω)\n", R_verify_known, R_known);
    printf("  Short circuit: Calculated R = %.2f Ω (should be 0.0 Ω)\n", R_verify_short);

    float error_known = fabs(R_verify_known - R_known) / R_known * 100.0f;
    float error_short = fabs(R_verify_short);
    printf("  Calibration errors: Known=%.1f%%, Short=%.2fΩ\n", error_known, error_short);

    // Interactive verification
    printf("\n[Step 3] Test different resistors in 0-15Ω range (press 'q' to quit).\n");
    while (true) {
        char key = wait_for_key();
        if (key == 'q')
            break;

        DifferentialReading test_reading = read_differential(8);

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
    err |= nvs_get_blob(handle, "v_gpio", &v_gpio, &required_size);
    err |= nvs_get_blob(handle, "r1_eff", &r1_eff, &required_size);
    err |= nvs_get_blob(handle, "r3_eff", &r3_eff, &required_size);

    nvs_close(handle);
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