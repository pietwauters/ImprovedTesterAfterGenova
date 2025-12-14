#pragma once

#include "driver/adc.h"
#include "esp_adc_cal.h"

// Low-level hardware abstraction for ADC measurements
// This layer knows HOW to read from hardware, but not WHAT the readings mean

namespace MeasurementHardware {

// Initialize ADC hardware (GPIO, channels, calibration)
void init_AD();

// Configure GPIO pins for a specific measurement configuration
// setting: bitmask for INPUT/OUTPUT configuration
// values: bitmask for HIGH/LOW values on output pins
void Set_IODirectionAndValue(uint8_t setting, uint8_t values);

// Get a single differential measurement between two ADC channels
// Returns: differential voltage in millivolts
// nr_samples: number of samples to take for filtering (default 16)
int getDifferentialSample(adc1_channel_t pin1, adc1_channel_t pin2, int nr_samples = 16);

// Convert raw ADC value to calibrated voltage
// Returns: voltage in millivolts
int getCalibratedVoltage(int raw_value, adc1_channel_t channel);

// Get pointer to ADC calibration characteristics (for advanced usage)
esp_adc_cal_characteristics_t* getADCCharacteristics();

}  // namespace MeasurementHardware
