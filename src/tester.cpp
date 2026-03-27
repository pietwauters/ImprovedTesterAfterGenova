#include "tester.h"

#include "DisplayManager.h"
#include "globals.h"  // For DoCalibration and other globals

// Global instance
Tester* testerInstance = nullptr;

// OLED is managed by DisplayManager

Tester::Tester(WS2812B_LedMatrix* ledPanelRef)
    : ledPanel(ledPanelRef),
      currentState(Waiting),
      timeToSwitch(WIRE_TEST_1_TIMEOUT),
      noWireTimeout(NO_WIRES_PLUGGED_IN_TIMEOUT),
      allGood(true),
      lastSpecialTestExit(0),
      testerTaskHandle(nullptr)

{
    testerInstance = this;
}

Tester::~Tester() {
    stop();
    testerInstance = nullptr;
}

void Tester::UpdateThresholdsWithLeadResistance(float RLead) {
    for (int i = 0; i < 11; i++) {
        myRefs_Ohm[i] = mycalibrator.get_mv_threshold(1.0f * i, RLead);
        // printf("Threshold[%d] = %d mV\n", i, myRefs_Ohm[i]);
    }
    Ohm_20 = mycalibrator.get_mv_threshold(20.0f, RLead);
    Ohm_50 = mycalibrator.get_mv_threshold(50.0f, RLead);

    // Lamé color breakpoints (×1, ×2, ×4 of LameThreshold)
    Lame_Green = mycalibrator.get_mv_threshold(LameThreshold, RLead);
    Lame_Yellow = mycalibrator.get_mv_threshold(LameThreshold * 2.0f, RLead);
    Lame_Orange = mycalibrator.get_mv_threshold(LameThreshold * 4.0f, RLead);

    // Foil body cord (loop) breakpoints
    FoilLoop_Green = mycalibrator.get_mv_threshold(FoilLoopThreshold, RLead);
    FoilLoop_Yellow = mycalibrator.get_mv_threshold(FoilLoopThreshold * 2.0f, RLead);
    FoilLoop_Orange = mycalibrator.get_mv_threshold(FoilLoopThreshold * 4.0f, RLead);

    // Foil/Epee tip wire (single wire) breakpoints
    FoilTip_Green = mycalibrator.get_mv_threshold(FoilSingleWireThreshold, RLead);
    FoilTip_Yellow = mycalibrator.get_mv_threshold(FoilSingleWireThreshold * 2.0f, RLead);
    FoilTip_Orange = mycalibrator.get_mv_threshold(FoilSingleWireThreshold * 4.0f, RLead);

    EpeeTip_Green = mycalibrator.get_mv_threshold(EpeeSingleWireThreshold, RLead);
    EpeeTip_Yellow = mycalibrator.get_mv_threshold(EpeeSingleWireThreshold * 2.0f, RLead);
    EpeeTip_Orange = mycalibrator.get_mv_threshold(EpeeSingleWireThreshold * 4.0f, RLead);

    // Foil/Epee probe (BrCl) breakpoints — shared setting
    Probe_Green = mycalibrator.get_mv_threshold(FoilMassProbeThreshold, RLead);
    Probe_Yellow = mycalibrator.get_mv_threshold(FoilMassProbeThreshold * 2.0f, RLead);
    Probe_Orange = mycalibrator.get_mv_threshold(FoilMassProbeThreshold * 4.0f, RLead);

    // Epee return wire (loop) breakpoints
    EpeeLoop_Green = mycalibrator.get_mv_threshold(EpeeLoopThreshold, RLead);
    EpeeLoop_Yellow = mycalibrator.get_mv_threshold(EpeeLoopThreshold * 2.0f, RLead);
    EpeeLoop_Orange = mycalibrator.get_mv_threshold(EpeeLoopThreshold * 4.0f, RLead);

    // Main wire test (body cord) thresholds (×1, ×2 green/yellow; ×10 broken)
    Bodycord_Green = mycalibrator.get_mv_threshold(BodycordThreshold, RLead);
    Bodycord_Yellow = mycalibrator.get_mv_threshold(BodycordThreshold * 2.0f, RLead);
    Bodycord_Broken = mycalibrator.get_mv_threshold(BodycordThreshold * 10.0f, RLead);

    // mycalibrator.print_roundtrip_diagnostics(RLead);
}

void Tester::begin(bool ForceCalibration) {
    Display.begin();
    mycalibrator.begin(br_analog, bl_analog);
    // Try to load existing calibration
    if ((ForceCalibration) || !mycalibrator.load_calibration_from_nvs()) {
        // No existing calibration, run interactive calibration
        mycalibrator.DoFactoryReset();
        DefaultBlinkColor = LedPanel->m_Red;

        if (!IgnoreCalibrationWarning) {
            LedPanel->Draw_C(LedPanel->m_Red);
            LedPanel->myShow();

            if (mycalibrator.calibrate_interactively_empirical()) {
                mycalibrator.save_calibration_to_nvs();
                LedPanel->ClearAll();
                LedPanel->myShow();

                DefaultBlinkColor = LedPanel->m_Green;
            } else {
                mycalibrator.DoFactoryReset();
                DefaultBlinkColor = LedPanel->m_Red;
            }
        }
    } else {
        DefaultBlinkColor = LedPanel->m_Green;
    }
    LedPanel->SetBlinkColor(DefaultBlinkColor);
    AverageLeadResistance = rtc.retrieve("LeadR", 0.0f);

    if (AverageLeadResistance > 0.0f) {
        LedPanel->SetBlinkColor(LedPanel->m_Blue);
    }
    UpdateThresholdsWithLeadResistance(0.0);
    SetWiretestMode(false);  // Normal mode, not Reel testing
    LedPanel->RestartBlink();

    // Below code lets you make a difference in lowpower time between cold boot and deep sleep
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        StartForLowPower = millis() + 90000;
    } else {
        StartForLowPower = 0;
    }
    /*
    long starttime = millis();
    for (int i = 0; i < 100; i++) {
        testWiresOnByOne();
        esp_task_wdt_reset();
    }
    long Duration = millis() - starttime;
    printf("Total duration = %d\n", Duration);
    printf("Time for a single sample = %d µs\n", Duration * 10 / 9);
*/
    // Create the tester task
    xTaskCreatePinnedToCore(testerTaskWrapper, "TesterTask",
                            8192,  // Stack size
                            this,  // Parameter passed to task
                            1,     // Priority
                            &testerTaskHandle,
                            1  // Core ID
    );
    // Display.clear();
}

void Tester::stop() {
    if (testerTaskHandle != nullptr) {
        vTaskDelete(testerTaskHandle);
        testerTaskHandle = nullptr;
    }
}

void Tester::testerTaskWrapper(void* parameter) {
    Tester* tester = static_cast<Tester*>(parameter);
    tester->taskLoop();
}

void Tester::taskLoop() {
    // Add watchdog for this task
    esp_task_wdt_add(NULL);

    while (true) {
        esp_task_wdt_reset();

        switch (currentState) {
            case Waiting:
                handleWaitingState();
                break;

            case WireTesting_1:
                handleWireTestingState1();
                break;

            case WireTesting_2:
                handleWireTestingState2();
                break;

            default:
                // Should not reach here
                currentState = Waiting;
                break;
        }

        esp_task_wdt_reset();
        vTaskDelay(5 / portTICK_PERIOD_MS);  // Small delay to prevent watchdog issues
    }
}

void Tester::handleWaitingState() {
    // Capture all 15 measurements once at the beginning
    Capture_.captureAll(currentMeasurements_);

    if (ReelMode) {
        if (ShowingShape != SHAPE_R) {
            LedPanel->ClearAll();
            LedPanel->Draw_R(LedPanel->m_Green);
            LedPanel->myShow();
            ShowingShape = SHAPE_R;
        }
        esp_task_wdt_reset();
        if (currentMeasurements_.get(Terminal::Al, Terminal::Bl) < Ohm_50) {
            currentState = Waiting;
            ShowingShape = SHAPE_NONE;
            LedPanel->ClearAll();
            LedPanel->myShow();
            SetWiretestMode(false);
            vTaskDelay(1000 / portTICK_PERIOD_MS);  // Small delay to prevent watchdog issues
        }

    } else {
        ledPanel->Blink();
#ifndef DOTHETRICK
        if (LowPowerMode) {
            if (!wifiPowerManager().getSecondsUntilTimeout() && (StartForLowPower < millis())) {
                if (!ledPanel->GetBlinkState()) {
                    LedPanel->ClearAll();
                    LedPanel->myShow();
                    // Store float value (lead resistance)
                    rtc.store("LeadR", AverageLeadResistance);
                    myDeepSleepHandler.enableTimerWakeup(2000000);
                    myDeepSleepHandler.enterDeepSleep();
                }
            }
        }
#endif
        // Check for special test modes

        if (currentMeasurements_.get(Terminal::Ar, Terminal::Cr) < Ohm_20) {
            currentState = EpeeTesting;
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doEpeeTest();
            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (currentMeasurements_.get(Terminal::Ar, Terminal::Br) < Ohm_20) {
            ledPanel->ClearAll();
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doFoilTest();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (currentMeasurements_.get(Terminal::Br, Terminal::Cr) < Ohm_20) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doLameTest();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if ((currentMeasurements_.get(Terminal::Cr, Terminal::Cl) < Ohm_20) &&
                   (currentMeasurements_.get(Terminal::Ar, Terminal::Al) > 160) &&
                   (currentMeasurements_.get(Terminal::Br, Terminal::Bl) > 160)) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance);
            doLameTest_Top();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (currentMeasurements_.get(Terminal::Al, Terminal::Bl) < Ohm_50) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doReelTest();
        }

        esp_task_wdt_reset();
    }
    // Check for wire testing mode with delay after special tests
    // Capture 3x3 matrix for wire detection (only right vs left, not right-to-right or left-to-left)
    Capture_.captureMatrix3x3(currentMeasurements_);
    if (MeasurementAnalysis::isWirePluggedIn(currentMeasurements_, Ohm_50)) {
        // Check if enough time has passed since last special test exit
        if (lastSpecialTestExit == 0 || (millis() - lastSpecialTestExit) > WIRE_TEST_DELAY) {
            UpdateThresholdsWithLeadResistance(0.0);

            currentState = WireTesting_1;
            noWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
            timeToSwitch = WIRE_TEST_1_TIMEOUT;
            ShowingShape = SHAPE_NONE;
            LedPanel->ClearAll();
            LedPanel->myShow();
        }
        // If not enough time has passed, stay in Waiting mode
    }
}

void Tester::handleWireTestingState1() {
    Display.setMode("Wiretest1");
    Display.showWiretesting1();
    Capture_.captureMatrix3x3(currentMeasurements_);

    int brBlValue = currentMeasurements_.get(Terminal::Br, Terminal::Bl);
    int crCLValue = currentMeasurements_.get(Terminal::Cr, Terminal::Cl);
    int arALValue = currentMeasurements_.get(Terminal::Ar, Terminal::Al);
    // printf("Rbb = %.1f\n", mycalibrator.get_resistance_empirical(brBlValue / 1000.0f));
    Display.showWiretesting1Values(mycalibrator.get_resistance_empirical(crCLValue / 1000.0f),
                                   mycalibrator.get_resistance_empirical(arALValue / 1000.0f),
                                   mycalibrator.get_resistance_empirical(brBlValue / 1000.0f));
    allGood = doQuickCheck();

    if (allGood) {
        timeToSwitch--;
    } else {
        timeToSwitch = WIRE_TEST_1_TIMEOUT;
        if (!MeasurementAnalysis::isWirePluggedIn(currentMeasurements_, ReferenceBroken)) {
            noWireTimeout--;
        } else {
            noWireTimeout = NO_WIRES_PLUGGED_IN_TIMEOUT;
        }
        if (!noWireTimeout) {
            currentState = Waiting;
            ShowingShape = SHAPE_NONE;
            Display.setMode("Waiting");
            Display.showMode();
            // SetWiretestMode(false);
        }
    }

    if (!timeToSwitch) {
        // The commented code set all lines to green for the next phase;
        // But this is confusing, because if you go to the next phase with yellow or orange
        // Everything suddenly becomes green
        /*for (int i = 0; i < 5; i += 2) {
            ledPanel->SetLine(i, ledPanel->m_Green);
        }*/
        doQuickCheck(false);  // check one more time (just to keep the correct colors)

        // This is the time to update the threasholds with the lead resistance

        Capture_.captureStraightOnly(currentMeasurements_);
        if (currentMeasurements_.get(Terminal::Cr, Terminal::Cl) < myRefs_Ohm[1] &&
            currentMeasurements_.get(Terminal::Ar, Terminal::Al) < myRefs_Ohm[1] &&
            currentMeasurements_.get(Terminal::Br, Terminal::Bl) < myRefs_Ohm[1]) {
            AverageLeadResistance = 0.0f;

            Terminal straightTerminals[3] = {Terminal::Cr, Terminal::Ar, Terminal::Br};
            Terminal leftTerminals[3] = {Terminal::Cl, Terminal::Al, Terminal::Bl};

            for (int i = 0; i < 3; i++) {
                Terminal rightTerm = straightTerminals[i];
                Terminal leftTerm = leftTerminals[i];
                int measurementValue = currentMeasurements_.get(rightTerm, leftTerm);
                leadresistances[i] = mycalibrator.get_resistance_empirical(measurementValue / 1000.0f);

                AverageLeadResistance += leadresistances[i];
                printf("Resistance lead[%d] = %.2f Ohm\n", i, leadresistances[i]);
                fflush(stdout);                 // Force flush
                vTaskDelay(pdMS_TO_TICKS(10));  // Small delay
            }
            if (AverageLeadResistance > 0.0f) {
                AverageLeadResistance /= 3.0f;
            } else {
                AverageLeadResistance = 0.0f;
            }
            printf("Average lead resistance = %f & setting blue\n", AverageLeadResistance);
            LedPanel->SetBlinkColor(LedPanel->m_Blue);
        }
        ledPanel->myShow();
        esp_task_wdt_reset();
        vTaskDelay(LOOP_DELAY_IN_WIRETESTING1 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        currentState = WireTesting_2;
    }
}

// Wiretesting2 is only looking for breaks. Resistances have been checked in phase 1
// So I'm using a relatively high and fixed value
void Tester::handleWireTestingState2() {
    Display.setMode("Wiretest2");
    Display.showWiretesting1();
    // Display.showMode();
    //  Initial capture to check if wires are still plugged in
    Capture_.captureStraightOnly(currentMeasurements_);
    int brBlValue = currentMeasurements_.get(Terminal::Br, Terminal::Bl);
    int crCLValue = currentMeasurements_.get(Terminal::Cr, Terminal::Cl);
    int arALValue = currentMeasurements_.get(Terminal::Ar, Terminal::Al);
    int AvAA = 0;
    int AvBB = 0;
    int AvCC = 0;

    while (MeasurementAnalysis::isWirePluggedIn(currentMeasurements_, ReferenceBroken)) {
        // Tight loop: capture AND test on every iteration, but keep them separate
        for (int i = 100000; i > 0; i--) {
            esp_task_wdt_reset();
            brBlValue = currentMeasurements_.get(Terminal::Br, Terminal::Bl);
            crCLValue = currentMeasurements_.get(Terminal::Cr, Terminal::Cl);
            arALValue = currentMeasurements_.get(Terminal::Ar, Terminal::Al);
            AvAA += arALValue;
            AvBB += brBlValue;
            AvCC += crCLValue;
            if (!(i % 10)) {
                Display.showWiretesting1Values(mycalibrator.get_resistance_empirical(AvCC / 10000.0f),
                                               mycalibrator.get_resistance_empirical(AvAA / 10000.0f),
                                               mycalibrator.get_resistance_empirical(AvBB / 10000.0f));
                AvAA = 0;
                AvBB = 0;
                AvCC = 0;
            }
            // Step 1: Test the captured data (pure logic)
            if (crCLValue >= ReferenceBroken || arALValue >= ReferenceBroken || brBlValue >= ReferenceBroken) {
                break;
            }
            // Step 2: Capture (separate function call) This has to be here to make sure we exit immediately if one of
            // the wires is set to red
            Capture_.captureStraightOnly(currentMeasurements_);
        }

        ledPanel->ClearAll();
        ledPanel->myShow();
        brBlValue = currentMeasurements_.get(Terminal::Br, Terminal::Bl);
        crCLValue = currentMeasurements_.get(Terminal::Cr, Terminal::Cl);
        arALValue = currentMeasurements_.get(Terminal::Ar, Terminal::Al);
        Display.showWiretesting1Values(mycalibrator.get_resistance_empirical(crCLValue / 1000.0f),
                                       mycalibrator.get_resistance_empirical(arALValue / 1000.0f),
                                       mycalibrator.get_resistance_empirical(brBlValue / 1000.0f));

        allGood &= animateSingleWire(currentMeasurements_, Terminal::Cr);
        allGood &= animateSingleWire(currentMeasurements_, Terminal::Ar);
        allGood &= animateSingleWire(currentMeasurements_, Terminal::Br);

        esp_task_wdt_reset();
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        ledPanel->ClearAll();
        ledPanel->myShow();
        Capture_.captureMatrix3x3(currentMeasurements_);
        doQuickCheck(false);  // check one more time (just to keep the correct colors)
    }
    timeToSwitch = WIRE_TEST_1_TIMEOUT;
    ledPanel->ClearAll();
    ledPanel->myShow();
    ShowingShape = SHAPE_NONE;
    currentState = Waiting;
    SetWiretestMode(false);
    Display.setMode("Waiting");
    Display.showMode();
}

void Tester::doCommonReturnFromSpecialMode() {
    currentState = Waiting;
    SetWiretestMode(false);
    esp_task_wdt_reset();
    ledPanel->ClearAll();
    ledPanel->RestartBlink();

    for (int i = 0; i < 25; i++) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        ledPanel->Blink();
        esp_task_wdt_reset();
    }

    ledPanel->ClearAll();
    Capture_.captureMatrix3x3(currentMeasurements_);
}

bool Tester::delayAndTestWirePluggedIn(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        Capture_.captureMatrix3x3(currentMeasurements_);
        if (MeasurementAnalysis::isWirePluggedIn(currentMeasurements_)) {
            return true;
        }
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInFoil(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        Capture_.captureMatrix3x3(currentMeasurements_);
        if (MeasurementAnalysis::isWirePluggedInFoil(currentMeasurements_)) {
            return true;
        }
        // While waiting, show the current Ar-Cl resistance value (or 9999.99 if unavailable)
        int raw = currentMeasurements_.get(Terminal::Ar, Terminal::Cl);
        if (raw == INT32_MAX) {
            raw = Capture_.captureSingle(Terminal::Ar, Terminal::Cl);
        }
        if (raw != INT32_MAX) {
            float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
            Display.showSingleValue(r);
        } else {
            Display.showSingleValue(9999.99);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInEpee(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        Capture_.captureMatrix3x3(currentMeasurements_);
        if (MeasurementAnalysis::isWirePluggedInEpee(currentMeasurements_)) {
            return true;
        }
    }
    return false;
}
bool Tester::delayAndTestWirePluggedInEpeeAndShowConnectionValue(long delay, Terminal Tfrom, Terminal Tto) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        Capture_.captureMatrix3x3(currentMeasurements_);
        if (MeasurementAnalysis::isWirePluggedInEpee(currentMeasurements_)) {
            return true;
        }
        // Try to use the cached 3x3 measurement first. If the pair is not present
        // in the matrix (e.g. both terminals on the same side), fall back to
        // performing a single capture for that pair.
        int raw = currentMeasurements_.get(Tfrom, Tto);
        if (raw == INT32_MAX) {
            raw = Capture_.captureSingle(Tfrom, Tto);
        }
        if (raw != INT32_MAX) {
            float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
            Display.showSingleValue(r);
        } else {
            Display.showSingleValue(9999.99);  // indicate invalid/unavailable
        }

        float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
        Display.showSingleValue(r);
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInLameTestTop(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        Capture_.captureMatrix3x3(currentMeasurements_);
        if (MeasurementAnalysis::isWirePluggedInLameTop(currentMeasurements_)) {
            return true;
        }
    }
    return false;
}

void Tester::doReelTest() {
    ShowingShape = SHAPE_R;
    LedPanel->ClearAll();
    LedPanel->Draw_R(LedPanel->m_Green);
    LedPanel->myShow();
    SetWiretestMode(true);

    // Initial capture before entering loop
    Capture_.captureMatrix3x3(currentMeasurements_);

    while (!MeasurementAnalysis::isWirePluggedInEpee(currentMeasurements_, ReferenceBroken)) {
        esp_task_wdt_reset();
        // Refresh measurements for next iteration
        Capture_.captureMatrix3x3(currentMeasurements_);
    }
    ShowingShape = SHAPE_NONE;
    LedPanel->ClearAll();
    LedPanel->myShow();
}

void Tester::doEpeeTest() {
    int BrCl;
    int arCr, arCl, arBr, brCr;  // Declare all loop variables at function scope
    uint32_t tempColor;
    ShowingShape = SHAPE_NONE;
    LedPanel->ClearAll();

    // Initial capture before entering loop
    Capture_.captureMatrix3x3(currentMeasurements_);
    Display.setMode("Epee");
    Display.showMode();

    while (!MeasurementAnalysis::isWirePluggedInEpee(currentMeasurements_)) {
        esp_task_wdt_reset();
        BrCl = Capture_.measureBrCl();
        if (BrCl < ProbeConnectedThreshold) {
            // We're in Probe mode
            if (SHAPE_P != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_P;
                Display.initForSingleValue("probe");
            }
            if (BrCl < Probe_Green) {
                LedPanel->Draw_P(LedPanel->m_Green);
            } else if (BrCl < Probe_Yellow) {
                LedPanel->Draw_P(LedPanel->m_Yellow);
            } else if (BrCl < Probe_Orange) {
                LedPanel->Draw_P(LedPanel->m_Orange);
            } else {
                LedPanel->Draw_P(LedPanel->m_Red);
            }
            LedPanel->myShow();
            float r = mycalibrator.get_resistance_empirical(BrCl / 1000.0f);
            Display.showSingleValue(r);
            /*if (delayAndTestWirePluggedInFoil(100)) {
                break;
            }*/
            vTaskDelay(100 / portTICK_PERIOD_MS);
            goto loop_end;
        }

        arCr = Capture_.measureArCr();
        arCl = Capture_.measureArCl();
        arBr = Capture_.measureArBr();
        brCr = Capture_.measureBrCr();

        // Case 1: Both ArBr and BrCr > ShortDetectThreshold, ArCl > EpeeTipContactThreshold
        // No shorts → measuring return wire (tip not touching probe)
        if ((arBr > ShortDetectThreshold && brCr > ShortDetectThreshold) && arCl > EpeeTipContactThreshold) {
            // Show color based on ArCr

            // printf("Rac = %.1f\n", mycalibrator.get_resistance_empirical(arCr / 1000.0f));
            if (arCr < EpeeLoop_Green) {
                tempColor = LedPanel->m_Green;
            } else if (arCr < EpeeLoop_Yellow) {
                tempColor = LedPanel->m_Yellow;
            } else if (arCr < EpeeLoop_Orange) {
                tempColor = LedPanel->m_Orange;
            } else {
                // Above 600, don't show anything
                if (SHAPE_E != ShowingShape) {
                    LedPanel->ClearAll();
                    ShowingShape = SHAPE_E;
                    LedPanel->Draw_E(LedPanel->m_White);
                }
                goto loop_end;
            }
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
                Display.initForSingleValue("a-b");
            }
            LedPanel->SetInner9(tempColor);
            LedPanel->myShow();
            float r = mycalibrator.get_resistance_empirical(arCr / 1000.0f);
            Display.showSingleValue(r);
            if (delayAndTestWirePluggedInEpeeAndShowConnectionValue(1000, Terminal::Ar, Terminal::Cr)) {
                break;
            }
            // LedPanel->ClearAll();
            LedPanel->myShow();
        }
        // Case 2: Both ArBr and BrCr > ShortDetectThreshold, ArCl < EpeeTipContactThreshold
        // No shorts → tip touching probe, measuring single wire
        else if ((arBr > ShortDetectThreshold && brCr > ShortDetectThreshold) && arCl < EpeeTipContactThreshold) {
            if (arCl < EpeeTip_Green) {
                tempColor = LedPanel->m_Green;
            } else if (arCl < EpeeTip_Yellow) {
                tempColor = LedPanel->m_Yellow;
            } else if (arCl < EpeeTip_Orange) {
                tempColor = LedPanel->m_Orange;
            } else {
                if (SHAPE_E != ShowingShape) {
                    LedPanel->ClearAll();
                    ShowingShape = SHAPE_E;
                    LedPanel->Draw_E(LedPanel->m_White);
                }
                goto loop_end;
            }
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
                Display.initForSingleValue("tip");
            }
            LedPanel->SetInner9(tempColor);
            LedPanel->myShow();
            float r = mycalibrator.get_resistance_empirical(arCl / 1000.0f);
            Display.showSingleValue(r);
            if (delayAndTestWirePluggedInEpeeAndShowConnectionValue(1000, Terminal::Ar, Terminal::Cl)) {
                break;
            }
            // LedPanel->ClearAll();

            LedPanel->myShow();
        }
        // Case 3: ArBr < ShortDetectThreshold or BrCr < ShortDetectThreshold (unwanted short)
        else if (arBr < ShortDetectThreshold || brCr < ShortDetectThreshold) {
            LedPanel->ClearAll();
            Display.initForSingleValue("mass");
            if (arBr < 1500) {
                float r = mycalibrator.get_resistance_empirical(arBr / 1000.0f);
                Display.showSingleValue(r);
                LedPanel->AnimateArBrConnection();
            }
            if (brCr < 1500) {
                float r = mycalibrator.get_resistance_empirical(brCr / 1000.0f);
                Display.showSingleValue(r);
                LedPanel->AnimateBrCrConnection();
            }
        }
        // All other cases: draw white E
        else {
            if (SHAPE_E != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_E;
                LedPanel->Draw_E(LedPanel->m_White);
                Display.initForSingleValue("a-b");
                Display.showSingleValue(9999.99);
            }
        }

        esp_task_wdt_reset();
    loop_end:
        // Refresh measurements for next iteration
        Capture_.captureMatrix3x3(currentMeasurements_);
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
    Display.clear();
}

void Tester::doFoilTest() {
    int BrCl;
    int arBr;  // Declare loop variable at function scope
    int ArCl;
    float r;
    // Initial capture before entering loop
    Capture_.captureMatrix3x3(currentMeasurements_);
    Display.setMode("Foil");
    Display.showMode();

    while (!MeasurementAnalysis::isWirePluggedInFoil(currentMeasurements_)) {
        esp_task_wdt_reset();
        LedPanel->myShow();
        BrCl = Capture_.measureBrCl();
        if (BrCl < ProbeConnectedThreshold) {  // Probe Mode
            if (SHAPE_P != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_P;
                Display.initForSingleValue("probe");
            }
            if (BrCl < Probe_Green) {
                LedPanel->Draw_P(LedPanel->m_Green);
            } else if (BrCl < Probe_Yellow) {
                LedPanel->Draw_P(LedPanel->m_Yellow);
            } else if (BrCl < Probe_Orange) {
                LedPanel->Draw_P(LedPanel->m_Orange);
            } else {
                LedPanel->Draw_P(LedPanel->m_Red);
            }
            r = mycalibrator.get_resistance_empirical(BrCl / 1000.0f);
            Display.showSingleValue(r);
            LedPanel->myShow();
            /*if (delayAndTestWirePluggedInFoil(100)) {
                break;
            }*/
            vTaskDelay(100 / portTICK_PERIOD_MS);
            goto loop_end;
        }

        arBr = Capture_.measureArBr();
        if (SHAPE_F != ShowingShape) {
            if (arBr < WireConnectedThreshold) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_F;
                Display.initForSingleValue("a_b");
            }
        }
        r = mycalibrator.get_resistance_empirical(arBr / 1000.0f);

        if (arBr < FoilLoop_Green) {
            LedPanel->Draw_F(LedPanel->m_Green);
            Display.showSingleValue(r);
            goto loop_end;
        } else if (arBr < FoilLoop_Yellow) {
            LedPanel->Draw_F(LedPanel->m_Yellow);
            Display.showSingleValue(r);
            goto loop_end;
        } else if (arBr < FoilLoop_Orange) {
            LedPanel->Draw_F(LedPanel->m_Orange);
            Display.showSingleValue(r);
            goto loop_end;
        } else {
            // Debounce: measureArBr() > FoilLoop_Orange must be true for 10ms
            bool debounced = true;
            unsigned long start = millis();
            while (millis() - start < 10) {
                esp_task_wdt_reset();
                if (Capture_.measureArBr() <= FoilLoop_Orange) {
                    debounced = false;
                    break;
                }
                taskYIELD();
            }

            if (!debounced) {
                goto loop_end;
            }

            // Debouncing succeeded: show inner lights based on ArCl
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
                Display.initForSingleValue("tip");
            }
            ArCl = Capture_.measureArCl();
            if (ArCl < FoilTip_Green) {
                LedPanel->SetInner9(LedPanel->m_Green);
            } else if (ArCl < FoilTip_Yellow) {
                LedPanel->SetInner9(LedPanel->m_Yellow);
            } else if (ArCl < FoilTip_Orange) {
                LedPanel->SetInner9(LedPanel->m_Orange);
            } else {
                LedPanel->SetInner9(LedPanel->m_White);
            }

            r = mycalibrator.get_resistance_empirical(ArCl / 1000.0f);
            Display.showSingleValue(r);
            LedPanel->myShow();

            if (delayAndTestWirePluggedInFoil(1000)) {
                break;
            }
            if (Capture_.measureArBr() <= ShortDetectThreshold) {
                LedPanel->ClearAll();
                LedPanel->myShow();
            }
        }
    loop_end:
        // Refresh measurements for next iteration
        Capture_.captureMatrix3x3(currentMeasurements_);
    }

    LedPanel->ClearAll();
    LedPanel->myShow();
    Display.clear();
}

// Display functions moved to DisplayManager
void Tester::doLameTest() {
    // Your existing DoLameTest code
    Display.setMode("Lame");
    Display.showMode();
    bool bShowingRed = false;
    Display.initForSingleValue("lame");

    // Initial capture before entering loop
    Capture_.captureMatrix3x3(currentMeasurements_);
    float r = 0.0f;
    while (!MeasurementAnalysis::isWirePluggedIn(currentMeasurements_)) {
        esp_task_wdt_reset();

        if (int raw = Capture_.measureBrCr(); raw < Lame_Green) {
            LedPanel->DrawDiamond(LedPanel->m_Green);
            bShowingRed = false;
            r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
            Display.showSingleValue(r);
            while (debouncedCondition(
                [this]() {
                    int temp = Capture_.measureBrCr();
                    float r = mycalibrator.get_resistance_empirical(temp / 1000.0f);
                    Display.showSingleValue(r);
                    return temp < Lame_Green;
                },
                10));
        } else {
            if (int raw = Capture_.measureBrCr(); raw < Lame_Yellow) {
                LedPanel->DrawDiamond(LedPanel->m_Yellow);
                bShowingRed = false;
                r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                Display.showSingleValue(r);
                while (debouncedCondition(
                    [this]() {
                        int temp = Capture_.measureBrCr();
                        float r = mycalibrator.get_resistance_empirical(temp / 1000.0f);
                        Display.showSingleValue(r);
                        return ((temp < Lame_Yellow) && (temp >= Lame_Green));
                    },
                    10));
            } else {
                if (int raw = Capture_.measureBrCr(); raw < Lame_Orange) {
                    LedPanel->DrawDiamond(LedPanel->m_Orange);
                    bShowingRed = false;
                    r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                    Display.showSingleValue(r);
                    while (debouncedCondition(
                        [this]() {
                            int temp = Capture_.measureBrCr();
                            float r = mycalibrator.get_resistance_empirical(temp / 1000.0f);
                            Display.showSingleValue(r);
                            return ((temp < Lame_Orange) && (temp >= Lame_Yellow));
                        },
                        10));
                } else {
                    // Do Red stuff
                    bShowingRed = true;
                    r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                    Display.showSingleValue(r);
                    LedPanel->DrawDiamond(LedPanel->m_Red);
                    LedPanel->setBuzz(true);
                    if (delayAndTestWirePluggedIn(250)) {
                        LedPanel->setBuzz(false);
                        break;
                    }
                    LedPanel->setBuzz(false);
                }
            }
        }

        esp_task_wdt_reset();
        // Refresh measurements for next iteration
        Capture_.captureMatrix3x3(currentMeasurements_);
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
    Display.clear();
}

bool Tester::DebounceTest(int LowBound, int HighBound) {
    Capture_.captureMatrix3x3(currentMeasurements_);
    if (MeasurementAnalysis::isWirePluggedInLameTop(currentMeasurements_)) {
        return false;
    }
    int crCl = currentMeasurements_.get(Terminal::Cr, Terminal::Cl);
    float r = mycalibrator.get_resistance_empirical(crCl / 1000.0f);
    Display.showSingleValue(r);
    return ((crCl >= LowBound) && (crCl < HighBound));
}

void Tester::doLameTest_Top() {
    // Your existing DoLameTest code
    Display.setMode("Lame");
    Display.showMode();
    bool bShowingRed = false;
    Display.initForSingleValue("lame");
    // Initial capture before entering loop
    Capture_.captureMatrix3x3(currentMeasurements_);

    while (!MeasurementAnalysis::isWirePluggedInLameTop(currentMeasurements_)) {
        esp_task_wdt_reset();
        if (int raw = Capture_.measureCrCl(); raw < Lame_Green) {
            LedPanel->DrawDiamond(LedPanel->m_Green);
            bShowingRed = false;
            float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
            Display.showSingleValue(r);
            while (debouncedCondition([this]() { return DebounceTest(0, Lame_Green); }, 10));
        } else {
            if (int raw = Capture_.measureCrCl(); raw < Lame_Yellow) {
                LedPanel->DrawDiamond(LedPanel->m_Yellow);
                bShowingRed = false;
                float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                Display.showSingleValue(r);
                while (debouncedCondition([this]() { return DebounceTest(Lame_Green, Lame_Yellow); }, 10));
            } else {
                if (int raw = Capture_.measureCrCl(); raw < Lame_Orange) {
                    LedPanel->DrawDiamond(LedPanel->m_Orange);
                    bShowingRed = false;
                    float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                    Display.showSingleValue(r);
                    while (debouncedCondition([this]() { return DebounceTest(Lame_Yellow, Lame_Orange); }, 10));
                } else {
                    LedPanel->DrawDiamond(LedPanel->m_Red);
                    bShowingRed = true;
                    LedPanel->setBuzz(true);
                    float r = mycalibrator.get_resistance_empirical(raw / 1000.0f);
                    Display.showSingleValue(r);
                    if (delayAndTestWirePluggedInLameTestTop(250)) {
                        LedPanel->setBuzz(false);
                        break;
                    }
                    LedPanel->setBuzz(true);
                }
            }
        }

        esp_task_wdt_reset();
        // Refresh measurements for next iteration
        Capture_.captureMatrix3x3(currentMeasurements_);
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
    Display.clear();
}

void Tester::SetWiretestMode(bool Reelmode) {
    if (Reelmode) {
        ReferenceBroken = Reel_Broken;
        ReferenceGreen = Reel_Green;
        ReferenceYellow = Reel_Yellow;
        ReferenceOrange = Reel_Broken;
        ReferenceShort = 300;
        ReelMode = true;
    } else {
        ReferenceBroken = Bodycord_Broken;
        ReferenceGreen = Bodycord_Green;
        ReferenceYellow = Bodycord_Yellow;
        ReferenceOrange = Bodycord_Broken;
        ReferenceShort = 160;
        ReelMode = false;
    }
}

bool Tester::animateSingleWire(const MeasurementSet& measurements, Terminal terminal) {
    // Map terminal to LED panel index for animations
    int wireIndex = (terminal == Terminal::Cr) ? 0 : (terminal == Terminal::Ar) ? 1 : 2;

    // Define the other two terminals for cross-connection checks
    Terminal otherTerminals[2];
    if (terminal == Terminal::Cr) {
        otherTerminals[0] = Terminal::Ar;
        otherTerminals[1] = Terminal::Br;
    } else if (terminal == Terminal::Ar) {
        otherTerminals[0] = Terminal::Br;
        otherTerminals[1] = Terminal::Cr;
    } else {  // Terminal::Br
        otherTerminals[0] = Terminal::Cr;
        otherTerminals[1] = Terminal::Ar;
    }

    // Map other terminals to their LED indices
    int otherIndex1 = (otherTerminals[0] == Terminal::Cr) ? 0 : (otherTerminals[0] == Terminal::Ar) ? 1 : 2;
    int otherIndex2 = (otherTerminals[1] == Terminal::Cr) ? 0 : (otherTerminals[1] == Terminal::Ar) ? 1 : 2;

    // Get straight-through measurement (right terminal to its corresponding left terminal)
    Terminal leftTerminal = (terminal == Terminal::Cr)   ? Terminal::Cl
                            : (terminal == Terminal::Ar) ? Terminal::Al
                                                         : Terminal::Bl;
    int straightMeasurement = measurements.get(terminal, leftTerminal);

    // Get cross-connection measurements
    Terminal leftOther1 = (otherTerminals[0] == Terminal::Cr)   ? Terminal::Cl
                          : (otherTerminals[0] == Terminal::Ar) ? Terminal::Al
                                                                : Terminal::Bl;
    Terminal leftOther2 = (otherTerminals[1] == Terminal::Cr)   ? Terminal::Cl
                          : (otherTerminals[1] == Terminal::Ar) ? Terminal::Al
                                                                : Terminal::Bl;
    int crossMeasurement1 = measurements.get(terminal, leftOther1);
    int crossMeasurement2 = measurements.get(terminal, leftOther2);

    bool bOK = false;
    if (straightMeasurement < ReferenceBroken) {
        if ((crossMeasurement1 > 200) && (crossMeasurement2 > 200)) {
            // OK - good straight connection, no cross-shorts
            int level = 2;
            if (straightMeasurement <= ReferenceYellow)
                level = 1;
            if (straightMeasurement <= ReferenceGreen)
                level = 0;

            LedPanel->AnimateGoodConnection(wireIndex, level);
            bOK = true;
        } else {
            // Short detected
            if (crossMeasurement1 < ReferenceShort)
                LedPanel->AnimateShort(wireIndex, otherIndex1);
            else if (crossMeasurement2 < ReferenceShort)
                LedPanel->AnimateShort(wireIndex, otherIndex2);
        }
    } else {
        if ((crossMeasurement1 > ReferenceShort) && (crossMeasurement2 > ReferenceShort)) {
            // Simply broken - high resistance on all connections
            LedPanel->AnimateBrokenConnection(wireIndex);
        } else {
            // Wrong connection - broken straight but has cross connection
            if (crossMeasurement1 < ReferenceShort)
                LedPanel->AnimateWrongConnection(wireIndex, otherIndex1);
            if (crossMeasurement2 < ReferenceShort)
                LedPanel->AnimateWrongConnection(wireIndex, otherIndex2);
        }
    }
    return bOK;
}

bool Tester::doQuickCheck(bool bClearAtTheEnd) {
    // Check all three wire connections using already-captured measurements
    bool bAllGood = true;

    bAllGood &= animateSingleWire(currentMeasurements_, Terminal::Cr);
    bAllGood &= animateSingleWire(currentMeasurements_, Terminal::Ar);
    bAllGood &= animateSingleWire(currentMeasurements_, Terminal::Br);

    esp_task_wdt_reset();
    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_task_wdt_reset();
    if (bClearAtTheEnd) {
        ledPanel->ClearAll();
        esp_task_wdt_reset();
    }

    vTaskDelay(300 / portTICK_PERIOD_MS);
    esp_task_wdt_reset();
    return bAllGood;
}

// Public getter methods
State_t Tester::getState() const { return currentState; }

void Tester::setState(State_t newState) { currentState = newState; }

bool Tester::isAllGood() const { return allGood; }

void Tester::startCalibration() { DoCalibration = true; }

void Tester::stopCalibration() { DoCalibration = false; }

// void Tester::setReferenceValues(int* refs) { myRefs_Ohm = refs; }
// int* Tester::getReferenceValues() const { return myRefs_Ohm; }

bool Tester::debouncedCondition(std::function<bool()> condition, int debounceMs) {
    unsigned long falseStartTime = 0;
    bool timing = false;

    while (true) {
        bool currentCondition = condition();

        if (currentCondition) {
            // Condition is true, reset timer and keep waiting
            timing = false;
            falseStartTime = 0;
        } else {
            if (!timing) {
                // Just became false, start timing
                falseStartTime = millis();
                timing = true;
            }
            // Check if it's been false long enough
            if (millis() - falseStartTime >= debounceMs) {
                return false;  // Debounced: condition has been false for debounceMs
            }
        }
        esp_task_wdt_reset();
        taskYIELD();
    }
}
