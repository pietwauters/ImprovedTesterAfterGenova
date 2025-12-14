#include "MeasurementHardware.h"

#include <Arduino.h>

#include "Hardware.h"
#include "esp_task_wdt.h"

// Number of ADC samples to take for each measurement
constexpr int MAX_NUM_ADC_SAMPLES = 64;
constexpr int NUM_ADC_SAMPLES = 16;

// Driver pin mapping
const uint8_t driverpins[] = {al_driver, bl_driver, cl_driver, ar_driver, br_driver, cr_driver, piste_driver};

// ADC calibration characteristics
static esp_adc_cal_characteristics_t adc_chars;

// Sample buffers (reused for efficiency)
static int samples1[MAX_NUM_ADC_SAMPLES];
static int samples2[MAX_NUM_ADC_SAMPLES];

namespace MeasurementHardware {

void Set_IODirectionAndValue(uint8_t setting, uint8_t values) {
    uint8_t mask = 1;
    for (int i = 0; i < 7; i++) {
        if (setting & mask) {
            pinMode(driverpins[i], INPUT);
        } else {
            pinMode(driverpins[i], OUTPUT);
            if (values & mask) {
                digitalWrite(driverpins[i], HIGH);
            } else {
                digitalWrite(driverpins[i], LOW);
            }
        }
        mask <<= 1;
    }
}

int getCalibratedVoltage(int raw_value, adc1_channel_t channel) {
    return esp_adc_cal_raw_to_voltage(raw_value, &adc_chars);
}

int getDifferentialSample(adc1_channel_t pin1, adc1_channel_t pin2, int nr_samples) {
    // Collect samples from each pin
    for (int i = 0; i < nr_samples; i++) {
        esp_task_wdt_reset();
        samples1[i] = adc1_get_raw(pin1);
        esp_task_wdt_reset();
        samples2[i] = adc1_get_raw(pin2);
    }

    // Simple insertion sort for NUM_ADC_SAMPLES elements (very fast)
    for (int i = 1; i < nr_samples; i++) {
        int key1 = samples1[i];
        int key2 = samples2[i];
        int j = i - 1;
        while (j >= 0 && samples1[j] > key1) {
            samples1[j + 1] = samples1[j];
            samples2[j + 1] = samples2[j];
            j--;
        }
        samples1[j + 1] = key1;
        samples2[j + 1] = key2;
    }

    // Use trimmed mean (skip top and bottom 10% of samples)
    int trim_count = nr_samples / 10;  // Remove 10% from each end (20% total)
    if (trim_count < 1)
        trim_count = 1;  // Always remove at least 1 sample from each end if we have enough samples
    if (nr_samples <= 4)
        trim_count = 0;  // Don't trim if we have too few samples

    int start_idx = trim_count;
    int end_idx = nr_samples - trim_count;
    int valid_samples = end_idx - start_idx;

    // Calculate trimmed mean for both sample arrays
    long sum1 = 0, sum2 = 0;
    for (int i = start_idx; i < end_idx; i++) {
        sum1 += samples1[i];
        sum2 += samples2[i];
    }

    int trimmed_mean1 = sum1 / valid_samples;
    int trimmed_mean2 = sum2 / valid_samples;

    int delta = (getCalibratedVoltage(trimmed_mean1, pin1) - getCalibratedVoltage(trimmed_mean2, pin2));
    return delta;
}

void init_AD() {
    gpio_set_drive_capability(GPIO_NUM_33, GPIO_DRIVE_CAP_3);  // 40 mA
    gpio_set_drive_capability(GPIO_NUM_21, GPIO_DRIVE_CAP_3);  // 40 mA
    gpio_set_drive_capability(GPIO_NUM_23, GPIO_DRIVE_CAP_3);  // 40 mA
    gpio_set_drive_capability(GPIO_NUM_25, GPIO_DRIVE_CAP_3);  // 40 mA
    gpio_set_drive_capability(GPIO_NUM_5, GPIO_DRIVE_CAP_3);   // 40 mA
    gpio_set_drive_capability(GPIO_NUM_18, GPIO_DRIVE_CAP_3);  // 40 mA
    gpio_set_drive_capability(GPIO_NUM_19, GPIO_DRIVE_CAP_3);  // 40 mA

    Set_IODirectionAndValue(IODirection_ar_bl, IOValues_ar_bl);
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    // Warm up ADC with some dummy readings
    int test = adc1_get_raw(ADC1_CHANNEL_3);
    test = adc1_get_raw(ADC1_CHANNEL_4);
    test = adc1_get_raw(ADC1_CHANNEL_5);
    test = adc1_get_raw(ADC1_CHANNEL_6);
    test = adc1_get_raw(ADC1_CHANNEL_7);
    test = adc1_get_raw(ADC1_CHANNEL_0);
    (void)test;  // Suppress unused variable warning
}

esp_adc_cal_characteristics_t* getADCCharacteristics() { return &adc_chars; }

}  // namespace MeasurementHardware
