#pragma once

#include "driver/adc.h"
#include "esp_adc_cal.h"

class DifferentialResistorCalibrator {
   public:
    // Initialize with top and bottom ADC channels
    bool begin(adc1_channel_t adc_channel_top, adc1_channel_t adc_channel_bottom);

    // Interactive calibration using known resistor
    bool calibrate_interactively(float R_known);

    // Get resistance from differential measurement
    float get_resistance_from_differential(float v_top, float v_bottom);

    // Get ADC threshold for a given resistance (differential mode)
    int get_adc_threshold_for_resistance(float R);

    // Save/load calibration
    bool save_calibration_to_nvs(const char* nvs_namespace = "diff_cal");
    bool load_calibration_from_nvs(const char* nvs_namespace = "diff_cal");

    // Measurement functions
    struct DifferentialReading {
        float v_top;
        float v_bottom;
        float v_diff;
        float resistance;
    };

    DifferentialReading read_differential(int samples = 8);
    DifferentialReading read_differential_average(int samples = 100);

    // Getters
    float get_vcc() const { return v_gpio; }
    float get_r1_eff() const { return r1_eff; }
    float get_r3_eff() const { return r3_eff; }

   private:
    // ADC channels
    adc1_channel_t channel_top;
    adc1_channel_t channel_bottom;

    // Calibration parameters
    float v_gpio = 0.0f;  // Supply voltage
    float r1_eff = 0.0f;  // Effective R1 (47Ω + GPIO Ron + parasitic resistances)
    float r3_eff = 0.0f;  // Effective R3 (47Ω + GPIO Ron + parasitic resistances)

    // ADC calibration
    esp_adc_cal_characteristics_t adc_chars;

    // Helper functions
    float read_voltage_single(adc1_channel_t channel);
    float read_voltage_average(adc1_channel_t channel, int samples);
    int voltage_to_adc_raw(float voltage);
    void wait_for_enter();
    char wait_for_key();

    // Fast ADC read (you can replace with your FastADC1 if needed)
    inline uint32_t fast_adc1_get_raw_inline(adc1_channel_t channel) { return adc1_get_raw(channel); }
};