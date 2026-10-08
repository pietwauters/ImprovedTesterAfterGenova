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

// The two halves of a bidirectional measurement (millivolts, trimmed means)
struct DifferentialDetail {
    bool reversed;  // false: the drive could not be reversed, only the forward fields are set
    int fwd_mv1;    // forward half: pin1, pin2 as driven by Set_IODirectionAndValue
    int fwd_mv2;
    int rev_mv1;  // reversed half: same pins, the other output pin high
    int rev_mv2;
};

// Get a single differential measurement between two ADC channels, for the drive set by the last
// Set_IODirectionAndValue(). Half of the samples are taken with that drive, half with the current
// reversed (the other of the two output pins high); the result is the average of
// (V1 - V2) forward and (V2 - V1) reversed. That cancels offset and gain differences between
// the two ADC channels and part of the ADC's nonlinearity. The forward drive is restored.
// Returns: differential voltage in millivolts
// nr_samples: number of samples to take for filtering (default 16, both halves together)
int getDifferentialSample(adc1_channel_t pin1, adc1_channel_t pin2, int nr_samples = 16,
                          DifferentialDetail* detail = nullptr);

// Drive values with the other of the two output pins high; 0 if the setting drives not exactly two pins
uint8_t reversedDriveValues(uint8_t setting, uint8_t values);

// Convert raw ADC value to calibrated voltage
// Returns: voltage in millivolts
int getCalibratedVoltage(int raw_value, adc1_channel_t channel);

// Get pointer to ADC calibration characteristics (for advanced usage)
esp_adc_cal_characteristics_t* getADCCharacteristics();

}  // namespace MeasurementHardware
