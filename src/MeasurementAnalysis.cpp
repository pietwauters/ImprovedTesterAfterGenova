#include "MeasurementAnalysis.h"

// ============================================================================
// New API - using MeasurementSet
// ============================================================================

bool MeasurementAnalysis::isWirePluggedIn(const MeasurementSet& data, int threshold) {
    // Check if any measurement is below threshold
    for (uint8_t i = 0; i < data.count(); i++) {
        if (data[i].isValid() && data[i].millivolts() < threshold) {
            return true;
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInFoil(const MeasurementSet& data, int threshold) {
    // For Foil testing: Skip Lamé wire connections (Ar-Cl and Br-Cl)
    // Check all other measurements
    for (uint8_t i = 0; i < data.count(); i++) {
        const auto& m = data[i];
        if (!m.isValid())
            continue;

        // Skip Ar-Cl and Br-Cl (Lamé connections)
        if ((m.matches(Terminal::Ar, Terminal::Cl)) || (m.matches(Terminal::Br, Terminal::Cl))) {
            continue;
        }

        if (m.millivolts() < threshold) {
            return true;
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInEpee(const MeasurementSet& data, int threshold) {
    // For Epee testing: Only check measurements to Al and Bl (not Cl)
    for (uint8_t i = 0; i < data.count(); i++) {
        const auto& m = data[i];
        if (!m.isValid())
            continue;

        // Only check connections to Al or Bl
        if (m.matches(Terminal::Ar, Terminal::Al) || m.matches(Terminal::Ar, Terminal::Bl) ||
            m.matches(Terminal::Br, Terminal::Al) || m.matches(Terminal::Br, Terminal::Bl) ||
            m.matches(Terminal::Cr, Terminal::Al) || m.matches(Terminal::Cr, Terminal::Bl)) {
            if (m.millivolts() < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInLameTop(const MeasurementSet& data, int threshold) {
    // For Lamé top testing: Skip Cr row (only check Ar and Br)
    for (uint8_t i = 0; i < data.count(); i++) {
        const auto& m = data[i];
        if (!m.isValid())
            continue;

        // Only check Ar and Br connections
        if (m.matches(Terminal::Ar, Terminal::Cl) || m.matches(Terminal::Ar, Terminal::Al) ||
            m.matches(Terminal::Ar, Terminal::Bl) || m.matches(Terminal::Br, Terminal::Cl) ||
            m.matches(Terminal::Br, Terminal::Al) || m.matches(Terminal::Br, Terminal::Bl)) {
            if (m.millivolts() < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isBroken(const MeasurementSet& data, Terminal wire, int threshold) {
    // A wire is "simply broken" if:
    // 1. Its straight-through connection is above threshold (broken)
    // 2. It has no shorts to other wires

    // Check straight-through
    int32_t straightThrough = data.get(wire, wire);
    if (straightThrough == INT32_MAX || straightThrough < threshold) {
        return false;  // Either no data or not broken
    }

    // Check for shorts to other wires
    const Terminal allTerminals[] = {Terminal::Ar, Terminal::Br, Terminal::Cr,
                                     Terminal::Al, Terminal::Bl, Terminal::Cl};

    for (const Terminal& other : allTerminals) {
        if (other == wire)
            continue;

        int32_t crossConnection = data.get(wire, other);
        if (crossConnection != INT32_MAX && crossConnection < threshold) {
            return false;  // Has a short, not simply broken
        }
    }

    return true;  // Broken with no shorts
}

bool MeasurementAnalysis::isSwappedWith(const MeasurementSet& data, Terminal wire1, Terminal wire2, int threshold) {
    // Two wires are swapped if they have low resistance in both directions
    int32_t connection = data.get(wire1, wire2);

    if (connection == INT32_MAX)
        return false;

    return connection < threshold;
}

// ============================================================================
// Legacy API - using int[3][3] array
// ============================================================================

bool MeasurementAnalysis::isWirePluggedInLegacy(const int measurements[3][3], int threshold) {
    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 0; j < 3; j++) {
            if (measurements[Nr][j] < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInFoilLegacy(const int measurements[3][3], int threshold) {
    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 0; j < 3; j++) {
            // Skip the Lamé wire to A or B (Nr=1/2, j=0)
            if (!((Nr == 1 && j == 0) || (Nr == 2 && j == 0))) {
                if (measurements[Nr][j] < threshold) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInEpeeLegacy(const int measurements[3][3], int threshold) {
    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 1; j < 3; j++) {  // Skip j=0 (Cl)
            if (measurements[Nr][j] < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isWirePluggedInLameTopLegacy(const int measurements[3][3], int threshold) {
    for (int Nr = 1; Nr < 3; Nr++) {  // Skip Nr=0 (Cr)
        for (int j = 0; j < 3; j++) {
            if (measurements[Nr][j] < threshold) {
                return true;
            }
        }
    }
    return false;
}

bool MeasurementAnalysis::isBrokenLegacy(const int measurements[3][3], int wireIndex, int threshold) {
    if (wireIndex < 0 || wireIndex >= 3)
        return false;

    // Check if straight-through is broken
    if (measurements[wireIndex][wireIndex] < threshold)
        return false;

    // Check if no shorts to other wires
    for (int i = 0; i < 3; i++) {
        if (i != wireIndex) {
            if (measurements[wireIndex][i] < threshold)
                return false;
        }
    }

    return true;
}

bool MeasurementAnalysis::isSwappedWithLegacy(const int measurements[3][3], int i, int j, int threshold) {
    if (i < 0 || i >= 3 || j < 0 || j >= 3)
        return false;

    return (measurements[i][j] < threshold) && (measurements[j][i] < threshold);
}

// ============================================================================
// Helper methods
// ============================================================================

Terminal MeasurementAnalysis::indexToTerminal(int index) {
    // Map legacy array index to Terminal enum
    // measurements[Nr][j] where Nr=0:Cr, 1:Ar, 2:Br
    switch (index) {
        case 0:
            return Terminal::Cr;
        case 1:
            return Terminal::Ar;
        case 2:
            return Terminal::Br;
        default:
            return Terminal::Cr;
    }
}
