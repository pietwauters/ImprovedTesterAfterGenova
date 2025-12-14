#include "WireMeasurement.h"

#include <Arduino.h>

#include <cstring>

// ============================================================================
// Measurement Implementation
// ============================================================================

Measurement::Measurement()
    : from_(Terminal::Ar), to_(Terminal::Ar), millivolts_(INT32_MAX), timestamp_ms_(0), valid_(false) {}

Measurement::Measurement(Terminal from, Terminal to, int32_t mv, uint32_t ts)
    : from_(from), to_(to), millivolts_(mv), timestamp_ms_(ts == 0 ? millis() : ts), valid_(true) {
    // Normalize so from < to (canonical ordering)
    if (static_cast<uint8_t>(from_) > static_cast<uint8_t>(to_)) {
        Terminal temp = from_;
        from_ = to_;
        to_ = temp;
    }
}

float Measurement::getResistance(const EmpiricalResistorCalibrator& cal) const {
    if (!valid_) {
        return -1.0f;
    }
    return cal.get_resistance_empirical(millivolts_ / 1000.0f);
}

bool Measurement::matches(Terminal t1, Terminal t2) const {
    // Normalize input for comparison
    if (static_cast<uint8_t>(t1) > static_cast<uint8_t>(t2)) {
        Terminal temp = t1;
        t1 = t2;
        t2 = temp;
    }
    return (from_ == t1 && to_ == t2);
}

size_t Measurement::toBytes(uint8_t* buffer, size_t maxLen) const {
    // Binary format: [from:1][to:1][valid:1][reserved:1][millivolts:4][timestamp:4] = 12 bytes
    if (maxLen < 12) {
        return 0;
    }

    buffer[0] = static_cast<uint8_t>(from_);
    buffer[1] = static_cast<uint8_t>(to_);
    buffer[2] = valid_ ? 1 : 0;
    buffer[3] = 0;  // reserved

    memcpy(&buffer[4], &millivolts_, 4);
    memcpy(&buffer[8], &timestamp_ms_, 4);

    return 12;
}

Measurement Measurement::fromBytes(const uint8_t* buffer, size_t len, bool& success) {
    success = false;

    if (len < 12) {
        return Measurement();
    }

    Terminal from = static_cast<Terminal>(buffer[0]);
    Terminal to = static_cast<Terminal>(buffer[1]);
    bool valid = (buffer[2] != 0);

    int32_t mv;
    uint32_t ts;
    memcpy(&mv, &buffer[4], 4);
    memcpy(&ts, &buffer[8], 4);

    // Validate terminal values
    if (static_cast<uint8_t>(from) >= static_cast<uint8_t>(Terminal::COUNT) ||
        static_cast<uint8_t>(to) >= static_cast<uint8_t>(Terminal::COUNT)) {
        return Measurement();
    }

    success = true;
    Measurement m(from, to, mv, ts);
    if (!valid) {
        m.invalidate();
    }
    return m;
}

// ============================================================================
// MeasurementSet Implementation
// ============================================================================

MeasurementSet::MeasurementSet() : count_(0), timestamp_ms_(millis()) {
    lead_resistances_[0] = 0.0f;
    lead_resistances_[1] = 0.0f;
    lead_resistances_[2] = 0.0f;
}

bool MeasurementSet::add(Terminal from, Terminal to, int32_t mv) {
    if (count_ >= MAX_MEASUREMENTS) {
        return false;
    }

    // Check if this measurement already exists - if so, update it
    for (uint8_t i = 0; i < count_; i++) {
        if (measurements_[i].matches(from, to)) {
            measurements_[i] = Measurement(from, to, mv);
            return true;
        }
    }

    // Add new measurement
    measurements_[count_] = Measurement(from, to, mv);
    count_++;
    return true;
}

bool MeasurementSet::add(const Measurement& measurement) {
    if (count_ >= MAX_MEASUREMENTS) {
        return false;
    }

    // Check if this measurement already exists
    for (uint8_t i = 0; i < count_; i++) {
        if (measurements_[i].matches(measurement.from(), measurement.to())) {
            measurements_[i] = measurement;
            return true;
        }
    }

    measurements_[count_] = measurement;
    count_++;
    return true;
}

void MeasurementSet::clear() {
    count_ = 0;
    timestamp_ms_ = millis();
}

const Measurement* MeasurementSet::find(Terminal from, Terminal to) const {
    for (uint8_t i = 0; i < count_; i++) {
        if (measurements_[i].matches(from, to)) {
            return &measurements_[i];
        }
    }
    return nullptr;
}

bool MeasurementSet::has(Terminal from, Terminal to) const { return find(from, to) != nullptr; }

int32_t MeasurementSet::get(Terminal from, Terminal to) const {
    const Measurement* m = find(from, to);
    if (m && m->isValid()) {
        return m->millivolts();
    }
    return INT32_MAX;
}

void MeasurementSet::setLeadResistance(int wire, float resistance) {
    if (wire >= 0 && wire < 3) {
        lead_resistances_[wire] = resistance;
    }
}

float MeasurementSet::getLeadResistance(int wire) const {
    if (wire >= 0 && wire < 3) {
        return lead_resistances_[wire];
    }
    return 0.0f;
}

float MeasurementSet::getAverageLeadResistance() const {
    float sum = 0.0f;
    int valid_count = 0;

    for (int i = 0; i < 3; i++) {
        if (lead_resistances_[i] > 0.0f) {
            sum += lead_resistances_[i];
            valid_count++;
        }
    }

    return (valid_count > 0) ? (sum / valid_count) : 0.0f;
}

const Measurement& MeasurementSet::operator[](size_t idx) const {
    // Note: No bounds checking for performance - caller must check count()
    return measurements_[idx];
}

size_t MeasurementSet::toJson(char* buffer, size_t maxLen) const {
    size_t offset = 0;

    // Start JSON object
    offset += snprintf(buffer + offset, maxLen - offset, "{\"timestamp\":%u,", timestamp_ms_);

    // Lead resistances
    offset += snprintf(buffer + offset, maxLen - offset, "\"lead_r\":[%.3f,%.3f,%.3f],", lead_resistances_[0],
                       lead_resistances_[1], lead_resistances_[2]);

    // Measurements array
    offset += snprintf(buffer + offset, maxLen - offset, "\"measurements\":[");

    for (uint8_t i = 0; i < count_; i++) {
        const Measurement& m = measurements_[i];
        if (i > 0) {
            offset += snprintf(buffer + offset, maxLen - offset, ",");
        }
        offset += snprintf(buffer + offset, maxLen - offset, "{\"from\":\"%s\",\"to\":\"%s\",\"mv\":%d,\"valid\":%s}",
                           terminalToString(m.from()), terminalToString(m.to()), m.millivolts(),
                           m.isValid() ? "true" : "false");

        if (offset >= maxLen - 100) {  // Safety margin
            break;
        }
    }

    offset += snprintf(buffer + offset, maxLen - offset, "]}");

    return offset;
}

bool MeasurementSet::fromJson(const char* jsonStr) {
    // Simple JSON parser - for production, consider using ArduinoJson library
    // This is a basic implementation that expects well-formed JSON
    // TODO: Implement if needed for your cross-ESP communication
    return false;
}

size_t MeasurementSet::toBytes(uint8_t* buffer, size_t maxLen) const {
    // Binary format: [timestamp:4][lead_r0:4][lead_r1:4][lead_r2:4][count:1][measurements...]
    size_t headerSize = 4 + 4 + 4 + 4 + 1;  // 17 bytes
    size_t measurementSize = 12;            // per measurement

    size_t requiredSize = headerSize + (count_ * measurementSize);
    if (maxLen < requiredSize) {
        return 0;
    }

    size_t offset = 0;

    // Timestamp
    memcpy(&buffer[offset], &timestamp_ms_, 4);
    offset += 4;

    // Lead resistances
    memcpy(&buffer[offset], &lead_resistances_[0], 4);
    offset += 4;
    memcpy(&buffer[offset], &lead_resistances_[1], 4);
    offset += 4;
    memcpy(&buffer[offset], &lead_resistances_[2], 4);
    offset += 4;

    // Count
    buffer[offset] = count_;
    offset += 1;

    // Measurements
    for (uint8_t i = 0; i < count_; i++) {
        size_t written = measurements_[i].toBytes(&buffer[offset], maxLen - offset);
        if (written == 0) {
            return 0;  // Failed
        }
        offset += written;
    }

    return offset;
}

MeasurementSet MeasurementSet::fromBytes(const uint8_t* buffer, size_t len, bool& success) {
    success = false;
    MeasurementSet set;

    if (len < 17) {  // Minimum header size
        return set;
    }

    size_t offset = 0;

    // Timestamp
    memcpy(&set.timestamp_ms_, &buffer[offset], 4);
    offset += 4;

    // Lead resistances
    memcpy(&set.lead_resistances_[0], &buffer[offset], 4);
    offset += 4;
    memcpy(&set.lead_resistances_[1], &buffer[offset], 4);
    offset += 4;
    memcpy(&set.lead_resistances_[2], &buffer[offset], 4);
    offset += 4;

    // Count
    uint8_t count = buffer[offset];
    offset += 1;

    if (count > MAX_MEASUREMENTS) {
        return set;
    }

    // Measurements
    for (uint8_t i = 0; i < count; i++) {
        bool measurementSuccess = false;
        Measurement m = Measurement::fromBytes(&buffer[offset], len - offset, measurementSuccess);

        if (!measurementSuccess) {
            return set;
        }

        set.add(m);
        offset += 12;  // Measurement size
    }

    success = true;
    return set;
}

void MeasurementSet::print() const {
    Serial.printf("MeasurementSet @ %u ms (%d measurements)\n", timestamp_ms_, count_);
    Serial.printf("  Lead resistances: [%.3f, %.3f, %.3f] Ohm\n", lead_resistances_[0], lead_resistances_[1],
                  lead_resistances_[2]);

    for (uint8_t i = 0; i < count_; i++) {
        const Measurement& m = measurements_[i];
        Serial.printf("  [%d] %s-%s: %d mV (%s)\n", i, terminalToString(m.from()), terminalToString(m.to()),
                      m.millivolts(), m.isValid() ? "valid" : "INVALID");
    }
}

// ============================================================================
// Helper Functions
// ============================================================================

const char* terminalToString(Terminal t) {
    switch (t) {
        case Terminal::Ar:
            return "Ar";
        case Terminal::Br:
            return "Br";
        case Terminal::Cr:
            return "Cr";
        case Terminal::Al:
            return "Al";
        case Terminal::Bl:
            return "Bl";
        case Terminal::Cl:
            return "Cl";
        default:
            return "??";
    }
}

Terminal stringToTerminal(const char* str) {
    if (strcmp(str, "Ar") == 0)
        return Terminal::Ar;
    if (strcmp(str, "Br") == 0)
        return Terminal::Br;
    if (strcmp(str, "Cr") == 0)
        return Terminal::Cr;
    if (strcmp(str, "Al") == 0)
        return Terminal::Al;
    if (strcmp(str, "Bl") == 0)
        return Terminal::Bl;
    if (strcmp(str, "Cl") == 0)
        return Terminal::Cl;
    return Terminal::Ar;  // Default
}
