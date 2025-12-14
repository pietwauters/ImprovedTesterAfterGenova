#include "MeasurementCapture.h"

#include "Hardware.h"
#include "esp_task_wdt.h"

// Lookup tables for 3x3 matrix measurement configuration
static const uint8_t testsettings[][3][2] = {
    {{IODirection_cr_cl, IOValues_cr_cl},
     {IODirection_cr_piste, IOValues_cr_piste},
     {IODirection_cr_bl, IOValues_cr_bl}},
    {{IODirection_ar_cl, IOValues_ar_cl},
     {IODirection_ar_piste, IOValues_ar_piste},
     {IODirection_ar_bl, IOValues_ar_bl}},
    {{IODirection_br_cl, IOValues_br_cl},
     {IODirection_br_piste, IOValues_br_piste},
     {IODirection_br_bl, IOValues_br_bl}},
};

static const adc1_channel_t analogtestsettings[3] = {cl_analog, piste_analog, bl_analog};
static const adc1_channel_t analogtestsettings_right[3] = {cr_analog, ar_analog, br_analog};

void MeasurementCapture::captureMatrix3x3(MeasurementSet& result) {
    result.clear();
    uint32_t timestamp = millis();

    // Measure 3x3 matrix: [Cr,Ar,Br] vs [Cl,Al,Bl]
    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 0; j < 3; j++) {
            MeasurementHardware::Set_IODirectionAndValue(testsettings[Nr][j][0], testsettings[Nr][j][1]);
            int mv = MeasurementHardware::getDifferentialSample(analogtestsettings_right[Nr], analogtestsettings[j]);

            // Map to Terminal enum
            Terminal from = static_cast<Terminal>(Nr);   // 0=Cr, 1=Ar, 2=Br (but need to adjust)
            Terminal to = static_cast<Terminal>(j + 3);  // 0=Cl->3, 1=Al->4, 2=Bl->5 (but need to adjust)

            // Correct mapping based on arrays
            // analogtestsettings_right: [cr_analog, ar_analog, br_analog]
            // analogtestsettings: [cl_analog, piste_analog, bl_analog]
            Terminal from_mapped, to_mapped;
            switch (Nr) {
                case 0:
                    from_mapped = Terminal::Cr;
                    break;
                case 1:
                    from_mapped = Terminal::Ar;
                    break;
                case 2:
                    from_mapped = Terminal::Br;
                    break;
                default:
                    from_mapped = Terminal::Cr;
                    break;
            }

            switch (j) {
                case 0:
                    to_mapped = Terminal::Cl;
                    break;
                case 1:
                    to_mapped = Terminal::Al;
                    break;  // piste_analog
                case 2:
                    to_mapped = Terminal::Bl;
                    break;
                default:
                    to_mapped = Terminal::Cl;
                    break;
            }

            result.add(from_mapped, to_mapped, mv);
        }
    }

    result.setTimestamp(timestamp);
}

bool MeasurementCapture::captureStraightOnly(MeasurementSet& result, int threshold) {
    result.clear();
    bool allOK = true;
    uint32_t timestamp = millis();

    // Use high-resolution sampling for straight-through measurements
    constexpr int MAX_NUM_ADC_SAMPLES = 64;

    for (int Nr = 0; Nr < 3; Nr++) {
        MeasurementHardware::Set_IODirectionAndValue(testsettings[Nr][Nr][0], testsettings[Nr][Nr][1]);
        int mv = MeasurementHardware::getDifferentialSample(analogtestsettings_right[Nr], analogtestsettings[Nr],
                                                            MAX_NUM_ADC_SAMPLES);

        // Map to Terminal enum
        Terminal terminal;
        switch (Nr) {
            case 0:
                terminal = Terminal::Cr;
                break;
            case 1:
                terminal = Terminal::Ar;
                break;
            case 2:
                terminal = Terminal::Br;
                break;
            default:
                terminal = Terminal::Cr;
                break;
        }

        // Store as terminal to itself (straight through)
        result.add(terminal, terminal, mv);

        if (mv > threshold) {
            allOK = false;
        }
    }

    result.setTimestamp(timestamp);
    return allOK;
}

int MeasurementCapture::measureArBr() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_ar_br, IOValues_ar_br);
    return MeasurementHardware::getDifferentialSample(ar_analog, br_analog);
}

int MeasurementCapture::measureArCr() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_ar_cr, IOValues_ar_cr);
    return MeasurementHardware::getDifferentialSample(ar_analog, cr_analog);
}

int MeasurementCapture::measureArCl() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_ar_cl, IOValues_ar_cl);
    return MeasurementHardware::getDifferentialSample(ar_analog, cl_analog);
}

int MeasurementCapture::measureBrCr() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_br_cr, IOValues_br_cr);
    return MeasurementHardware::getDifferentialSample(br_analog, cr_analog);
}

int MeasurementCapture::measureBrCl() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_br_cl, IOValues_br_cl);
    return MeasurementHardware::getDifferentialSample(br_analog, cl_analog);
}

int MeasurementCapture::measureCrCl() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_cr_cl, IOValues_cr_cl);
    return MeasurementHardware::getDifferentialSample(cr_analog, cl_analog);
}

int MeasurementCapture::measureAlBl() {
    MeasurementHardware::Set_IODirectionAndValue(IODirection_cl_piste, IOValues_cl_piste);
    return MeasurementHardware::getDifferentialSample(cl_analog, piste_analog);
}

int MeasurementCapture::captureSingle(Terminal from, Terminal to) {
    // Map Terminal enum to specific measurement function
    // Note: This is order-independent due to Measurement's normalization

    if ((from == Terminal::Ar && to == Terminal::Br) || (from == Terminal::Br && to == Terminal::Ar)) {
        return measureArBr();
    } else if ((from == Terminal::Ar && to == Terminal::Cr) || (from == Terminal::Cr && to == Terminal::Ar)) {
        return measureArCr();
    } else if ((from == Terminal::Ar && to == Terminal::Cl) || (from == Terminal::Cl && to == Terminal::Ar)) {
        return measureArCl();
    } else if ((from == Terminal::Br && to == Terminal::Cr) || (from == Terminal::Cr && to == Terminal::Br)) {
        return measureBrCr();
    } else if ((from == Terminal::Br && to == Terminal::Cl) || (from == Terminal::Cl && to == Terminal::Br)) {
        return measureBrCl();
    } else if ((from == Terminal::Cr && to == Terminal::Cl) || (from == Terminal::Cl && to == Terminal::Cr)) {
        return measureCrCl();
    } else if ((from == Terminal::Al && to == Terminal::Bl) || (from == Terminal::Bl && to == Terminal::Al)) {
        return measureAlBl();
    }

    // Unsupported combination
    return INT32_MAX;
}

// Legacy compatibility functions
void MeasurementCapture::captureMatrix3x3Legacy(int measurements[3][3]) {
    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 0; j < 3; j++) {
            MeasurementHardware::Set_IODirectionAndValue(testsettings[Nr][j][0], testsettings[Nr][j][1]);
            measurements[Nr][j] =
                MeasurementHardware::getDifferentialSample(analogtestsettings_right[Nr], analogtestsettings[j]);
        }
    }
}

bool MeasurementCapture::captureStraightOnlyLegacy(int measurements[3][3], int threshold) {
    bool bOK = true;
    constexpr int MAX_NUM_ADC_SAMPLES = 64;

    for (int Nr = 0; Nr < 3; Nr++) {
        MeasurementHardware::Set_IODirectionAndValue(testsettings[Nr][Nr][0], testsettings[Nr][Nr][1]);
        measurements[Nr][Nr] = MeasurementHardware::getDifferentialSample(analogtestsettings_right[Nr],
                                                                          analogtestsettings[Nr], MAX_NUM_ADC_SAMPLES);

        if (measurements[Nr][Nr] > threshold) {
            bOK = false;
        }
    }

    return bOK;
}

// Populate MeasurementSet from legacy measurements[][] array
// Mapping: measurements[Nr][j] where Nr: 0=Cr, 1=Ar, 2=Br and j: 0=Cl, 1=Al, 2=Bl
void MeasurementCapture::populateFromLegacyArray(MeasurementSet& result, const int measurements[3][3]) {
    result.clear();
    uint32_t timestamp = millis();

    // Map array indices to Terminal enums
    const Terminal rightTerminals[3] = {Terminal::Cr, Terminal::Ar, Terminal::Br};
    const Terminal leftTerminals[3] = {Terminal::Cl, Terminal::Al, Terminal::Bl};

    for (int Nr = 0; Nr < 3; Nr++) {
        for (int j = 0; j < 3; j++) {
            result.add(rightTerminals[Nr], leftTerminals[j], measurements[Nr][j]);
        }
    }

    result.setTimestamp(timestamp);
}
