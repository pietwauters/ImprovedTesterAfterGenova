#pragma once

#include <Arduino.h>

#include "CalibrationPaths.h"
#include "adc_calibrator.h"

// Model types, so a future formula or measuring method can be stored next to the current one
enum CalModelType : uint8_t {
    CalModelEmpiricalV1 = 1,     // Rs + c/R on forward-only readings: outdated, not used
    CalModelEmpiricalBidir = 2,  // Rs + c/R on bidirectional readings: converted to M2 (Ri = -c/Rs) on load
    CalModelDividerRi = 3,       // M2, internal series resistance Ri (current)
};
const char* calModelTypeName(uint8_t type);

// Flags on a stored model
constexpr uint8_t CalModelMigrated = 0x01;  // copied from the legacy "emp_cal" calibration

struct StoredModel {
    uint16_t version;
    uint8_t type;  // CalModelType
    uint8_t flags;
    EmpiricalModel params;  // type 3: v_gpio, Rs, Ri. Types 1 and 2: v_gpio, Rs, c (in the r_internal slot)
    uint32_t runId;         // run record that produced it (0 = unknown)
};

// The model the tester can use from a stored one: type 3 as is, type 2 converted. False for type 1.
bool usableModel(const StoredModel& stored, EmpiricalModel& out);

// Persists calibration models, one per measurement path plus the previous one
// for undo, and a small ring of calibration run records for statistics.
// NVS namespace "cal_store". The firmware currently uses one model: the one of
// kDefaultCalibrationPath; lookups for other paths fall back to it.
class CalibrationStore {
   public:
    static constexpr int MaxRuns = 6;
    static constexpr size_t MaxRunBytes = 1536;

    // Model stored for exactly this path
    bool load(const CalibrationPath& path, StoredModel& out) const;
    // Model the tester can use for this path: its own, else the default path's, if usable
    bool loadActive(const CalibrationPath& path, EmpiricalModel& out, StoredModel* stored = nullptr) const;
    bool hasPrevious(const CalibrationPath& path) const;

    // Store a new model; the one it replaces becomes the previous model
    bool save(const CalibrationPath& path, const EmpiricalModel& params, uint32_t runId);
    // Swap the current and previous model of a path; returns the model now active
    bool undo(const CalibrationPath& path, StoredModel& restored);

    // Run records are compact JSON objects with an "id" member; the oldest is dropped when full
    bool addRun(const char* json, String& error);
    int runCount() const;
    // i = 0 is the oldest stored run
    bool getRun(int i, String& out) const;

   private:
    bool readModel(const char* key, StoredModel& out) const;
    bool writeModel(const char* key, const StoredModel& m);
};

extern CalibrationStore calibrationStore;
