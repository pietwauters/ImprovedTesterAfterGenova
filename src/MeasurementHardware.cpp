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

// Drive set by the last Set_IODirectionAndValue(), reversed by getDifferentialSample()
static uint8_t lastSetting = 0xFF;
static uint8_t lastValues = 0;

static void applyIO(uint8_t setting, uint8_t values);

namespace MeasurementHardware {

void Set_IODirectionAndValue(uint8_t setting, uint8_t values) {
    lastSetting = setting;
    lastValues = values;
    applyIO(setting, values);
}

uint8_t reversedDriveValues(uint8_t setting, uint8_t values) {
    uint8_t outputs = (uint8_t)~setting & 0x7F;  // 7 driver pins, a 0 bit is an output
    if (__builtin_popcount(outputs) != 2 || __builtin_popcount(outputs & values) != 1) {
        return 0;
    }
    return outputs & (uint8_t)~values;
}

}  // namespace MeasurementHardware

static void applyIO(uint8_t setting, uint8_t values) {
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

namespace MeasurementHardware {

int getCalibratedVoltage(int raw_value, adc1_channel_t channel) {
    return esp_adc_cal_raw_to_voltage(raw_value, &adc_chars);
}

// Trimmed means of the raw readings of both pins: samples are sorted by pin1 (pin2 travels with
// its pair) and 10 % is dropped at each end
static void trimmedMeansRaw(adc1_channel_t pin1, adc1_channel_t pin2, int nr_samples, int& mean1, int& mean2) {
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

    mean1 = sum1 / valid_samples;
    mean2 = sum2 / valid_samples;
}

int getDifferentialSample(adc1_channel_t pin1, adc1_channel_t pin2, int nr_samples, DifferentialDetail* detail) {
    if (nr_samples > MAX_NUM_ADC_SAMPLES) {
        nr_samples = MAX_NUM_ADC_SAMPLES;
    }
    uint8_t reversed = reversedDriveValues(lastSetting, lastValues);
    int forward_samples = (reversed != 0 && nr_samples >= 4) ? nr_samples / 2 : nr_samples;

    int raw1, raw2;
    trimmedMeansRaw(pin1, pin2, forward_samples, raw1, raw2);
    int fwd1 = getCalibratedVoltage(raw1, pin1);
    int fwd2 = getCalibratedVoltage(raw2, pin2);
    if (detail != nullptr) {
        detail->reversed = false;
        detail->fwd_mv1 = fwd1;
        detail->fwd_mv2 = fwd2;
    }
    if (forward_samples == nr_samples) {
        return fwd1 - fwd2;
    }

    // Second half with the current reversed: every node voltage mirrors, so V2 - V1 is the
    // forward difference again, measured at other ADC codes
    applyIO(lastSetting, reversed);
    trimmedMeansRaw(pin1, pin2, nr_samples - forward_samples, raw1, raw2);
    applyIO(lastSetting, lastValues);
    int rev1 = getCalibratedVoltage(raw1, pin1);
    int rev2 = getCalibratedVoltage(raw2, pin2);
    if (detail != nullptr) {
        detail->reversed = true;
        detail->rev_mv1 = rev1;
        detail->rev_mv2 = rev2;
    }
    // Average of both halves in whole mV. An odd sum is exactly x.5: round to even, because
    // always rounding up would read about 0.25 mV high on average (0.7 % at 1 Ohm)
    int sum = (fwd1 - fwd2) + (rev2 - rev1);
    int half = sum / 2;  // truncates toward zero
    if (sum % 2 != 0 && half % 2 != 0) {
        half += sum > 0 ? 1 : -1;
    }
    return half;
}

void init_AD() {
    // Boost drive strength to 40 mA on every terminal driver pin. Iterate the
    // driverpins[] table so this tracks the per-revision pin map in Hardware.h
    // instead of hard-coding GPIO numbers.
    for (uint8_t pin : driverpins) {
        gpio_set_drive_capability(static_cast<gpio_num_t>(pin), GPIO_DRIVE_CAP_3);
    }

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
