#pragma once

#include "WireMeasurement.h"

// Pure logic for analyzing measurement data
// This layer contains NO hardware interaction - only comparisons and logic
// All methods are static and work on provided data

class MeasurementAnalysis {
   public:
    // ========================================================================
    // Query methods using MeasurementSet (new clean API)
    // ========================================================================

    // Check if any wire is plugged in (any measurement below threshold)
    static bool isWirePluggedIn(const MeasurementSet& data, int threshold = 160);

    // Check if wire is plugged in for Foil testing (excludes Lamé to A/B)
    static bool isWirePluggedInFoil(const MeasurementSet& data, int threshold = 160);

    // Check if wire is plugged in for Epee testing (excludes column 0)
    static bool isWirePluggedInEpee(const MeasurementSet& data, int threshold = 160);

    // Check if wire is plugged in for Lamé top testing (excludes row 0)
    static bool isWirePluggedInLameTop(const MeasurementSet& data, int threshold = 160);

    // Check if a specific wire is broken (no connection to itself or others)
    static bool isBroken(const MeasurementSet& data, Terminal wire, int threshold);

    // Check if two wires are swapped with each other
    static bool isSwappedWith(const MeasurementSet& data, Terminal wire1, Terminal wire2, int threshold);

    // ========================================================================
    // Legacy methods using int[3][3] array (for backwards compatibility)
    // ========================================================================

    static bool isWirePluggedInLegacy(const int measurements[3][3], int threshold = 160);
    static bool isWirePluggedInFoilLegacy(const int measurements[3][3], int threshold = 160);
    static bool isWirePluggedInEpeeLegacy(const int measurements[3][3], int threshold = 160);
    static bool isWirePluggedInLameTopLegacy(const int measurements[3][3], int threshold = 160);
    static bool isBrokenLegacy(const int measurements[3][3], int wireIndex, int threshold);
    static bool isSwappedWithLegacy(const int measurements[3][3], int i, int j, int threshold);

   private:
    // Helper to map legacy wire index to Terminal enum
    static Terminal indexToTerminal(int index);
};
