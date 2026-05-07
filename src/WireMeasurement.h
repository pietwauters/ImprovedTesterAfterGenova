#pragma once

#include <Arduino.h>

#include <cstdint>

#include "adc_calibrator.h"

// Enum representing the 6 physical terminals
enum class Terminal : uint8_t {
    Ar = 0,  // A right
    Br = 1,  // B right
    Cr = 2,  // C right
    Al = 3,  // A left (piste)
    Bl = 4,  // B left
    Cl = 5,  // C left
    COUNT = 6
};

// Represents a single differential measurement between two terminals
class Measurement {
   private:
    Terminal from_;
    Terminal to_;
    int32_t millivolts_;
    uint32_t timestamp_ms_;
    bool valid_;

   public:
    // Default constructor - creates invalid measurement
    Measurement();

    // Constructor with data - creates valid measurement
    Measurement(Terminal from, Terminal to, int32_t mv, uint32_t ts = 0);

    // Getters
    Terminal from() const { return from_; }
    Terminal to() const { return to_; }
    int32_t millivolts() const { return millivolts_; }
    bool isValid() const { return valid_; }
    uint32_t timestamp() const { return timestamp_ms_; }

    // Calculate resistance using calibrator
    float getResistance(const EmpiricalResistorCalibrator& cal) const;

    // Invalidate this measurement
    void invalidate() { valid_ = false; }

    // Check if this measurement matches a terminal pair (order independent)
    bool matches(Terminal t1, Terminal t2) const;

    // Serialization helpers
    size_t toBytes(uint8_t* buffer, size_t maxLen) const;
    static Measurement fromBytes(const uint8_t* buffer, size_t len, bool& success);
};

// Container for a set of measurements (up to 15 possible combinations)
class MeasurementSet {
   private:
    static constexpr int MAX_MEASUREMENTS = 15;

    Measurement measurements_[MAX_MEASUREMENTS];
    uint8_t count_;
    float lead_resistances_[3];  // For Ar-Ar, Br-Br, Cr-Cr
    uint32_t timestamp_ms_;

   public:
    // Constructor
    MeasurementSet();

    // Add a measurement to the set
    // Returns true if added successfully, false if set is full
    bool add(Terminal from, Terminal to, int32_t mv);

    // Add an existing Measurement object
    bool add(const Measurement& measurement);

    // Clear all measurements
    void clear();

    // Query methods - find specific measurements
    const Measurement* find(Terminal from, Terminal to) const;
    bool has(Terminal from, Terminal to) const;
    int32_t get(Terminal from, Terminal to) const;  // Returns INT32_MAX if not found

    // Lead resistance management
    void setLeadResistance(int wire, float resistance);  // wire: 0=A, 1=B, 2=C
    float getLeadResistance(int wire) const;
    float getAverageLeadResistance() const;

    // Iteration support
    uint8_t count() const { return count_; }
    const Measurement& operator[](size_t idx) const;

    // Timestamp
    uint32_t timestamp() const { return timestamp_ms_; }
    void setTimestamp(uint32_t ts) { timestamp_ms_ = ts; }

    // Validation
    bool isEmpty() const { return count_ == 0; }
    bool isFull() const { return count_ == MAX_MEASUREMENTS; }

    // Serialization for cross-ESP communication
    size_t toJson(char* buffer, size_t maxLen) const;
    bool fromJson(const char* jsonStr);

    // Binary serialization (more efficient)
    size_t toBytes(uint8_t* buffer, size_t maxLen) const;
    static MeasurementSet fromBytes(const uint8_t* buffer, size_t len, bool& success);

    // Debug output
    void print() const;
};

// Helper function to convert Terminal enum to string
const char* terminalToString(Terminal t);

// Helper to get terminal from string
Terminal stringToTerminal(const char* str);
