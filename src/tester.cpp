#include "tester.h"

#include "globals.h"  // For DoCalibration and other globals

// Global instance
Tester* testerInstance = nullptr;

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
        myRefs_Ohm[i] = mycalibrator.get_adc_threshold_for_resistance_with_leads(1.0 * i, RLead);
        // printf("Threshold[%d] = %d\n", i, myRefs_Ohm[i]);
    }
    Ohm_20 = mycalibrator.get_adc_threshold_for_resistance_with_leads(20.0, RLead);
    Ohm_25 = mycalibrator.get_adc_threshold_for_resistance_with_leads(25.0, RLead);
    Ohm_30 = mycalibrator.get_adc_threshold_for_resistance_with_leads(30.0, RLead);
    Ohm_50 = mycalibrator.get_adc_threshold_for_resistance_with_leads(50.0, RLead);
}

void Tester::begin(bool ForceCalibration) {
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

    if (AverageLeadResistance > 0.0) {
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
        if (currentMeasurements_.get(Terminal::Al, Terminal::Cl) < Ohm_50) {
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
    if (MeasurementAnalysis::isWirePluggedIn(currentMeasurements_, ReferenceBroken)) {
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
    Capture_.captureMatrix3x3(currentMeasurements_);

    int brBlValue = currentMeasurements_.get(Terminal::Br, Terminal::Bl);
    printf("Rcc = %.1f\n", mycalibrator.get_resistance_empirical(brBlValue / 1000.0));
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
            AverageLeadResistance = 0.0;

            Terminal straightTerminals[3] = {Terminal::Cr, Terminal::Ar, Terminal::Br};
            Terminal leftTerminals[3] = {Terminal::Cl, Terminal::Al, Terminal::Bl};

            for (int i = 0; i < 3; i++) {
                Terminal rightTerm = straightTerminals[i];
                Terminal leftTerm = leftTerminals[i];
                int measurementValue = currentMeasurements_.get(rightTerm, leftTerm);
                leadresistances[i] = mycalibrator.get_resistance_empirical(measurementValue / 1000.0);

                AverageLeadResistance += leadresistances[i];
                printf("Resistance lead[%d] = %.2f Ohm\n", i, leadresistances[i]);
                fflush(stdout);                 // Force flush
                vTaskDelay(pdMS_TO_TICKS(10));  // Small delay
            }
            if (AverageLeadResistance > 0.0) {
                AverageLeadResistance /= 3.0;
            } else {
                AverageLeadResistance = 0.0;
            }
            printf("Average lead resistance = %f & setting blue\n", AverageLeadResistance);
            LedPanel->SetBlinkColor(LedPanel->m_Blue);
        }
        ledPanel->myShow();
        esp_task_wdt_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        currentState = WireTesting_2;
    }
}

// Wiretesting2 is only looking for breaks. Resistances have been checked in phase 1
// So I'm using a relatively high and fixed value
void Tester::handleWireTestingState2() {
    // Initial capture to check if wires are still plugged in
    Capture_.captureStraightOnly(currentMeasurements_);

    while (MeasurementAnalysis::isWirePluggedIn(currentMeasurements_, ReferenceBroken)) {
        // Tight loop: capture AND test on every iteration, but keep them separate
        for (int i = 100000; i > 0; i--) {
            esp_task_wdt_reset();

            // Step 1: Test the captured data (pure logic)
            if (currentMeasurements_.get(Terminal::Cr, Terminal::Cl) >= ReferenceBroken ||
                currentMeasurements_.get(Terminal::Ar, Terminal::Al) >= ReferenceBroken ||
                currentMeasurements_.get(Terminal::Br, Terminal::Bl) >= ReferenceBroken) {
                break;
            }
            // Step 2: Capture (separate function call) This has to be here to make sure we exit immediately if one of
            // the wires is set to red
            Capture_.captureStraightOnly(currentMeasurements_);
        }

        ledPanel->ClearAll();
        ledPanel->myShow();

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
    testWiresOnByOne();
}

bool Tester::delayAndTestWirePluggedIn(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        testWiresOnByOne();
        if (WirePluggedIn()) {
            return true;
        }
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInFoil(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        testWiresOnByOne();
        if (WirePluggedInFoil()) {
            return true;
        }
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInEpee(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        testWiresOnByOne();
        if (WirePluggedInEpee()) {
            return true;
        }
    }
    return false;
}

bool Tester::delayAndTestWirePluggedInLameTestTop(long delay) {
    long returnTime = millis() + delay;
    while (millis() < returnTime) {
        esp_task_wdt_reset();
        testWiresOnByOne();
        if (WirePluggedInLameTopTesting()) {
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
    while (!WirePluggedInEpee(ReferenceBroken)) {
        esp_task_wdt_reset();
        testWiresOnByOne();
    }
    ShowingShape = SHAPE_NONE;
    LedPanel->ClearAll();
    LedPanel->myShow();
}

void Tester::doEpeeTest() {
    int BrCl;
    uint32_t tempColor;
    testWiresOnByOne();
    ShowingShape = SHAPE_NONE;
    LedPanel->ClearAll();
    while (!WirePluggedInEpee()) {
        esp_task_wdt_reset();
        BrCl = testBrCl();
        if (BrCl < 500) {
            // We're in Probe mode
            if (SHAPE_P != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_P;
            }
            if (BrCl < myRefs_Ohm[5]) {
                LedPanel->Draw_P(LedPanel->m_Green);
            } else {
                if (BrCl < myRefs_Ohm[8]) {
                    LedPanel->Draw_P(LedPanel->m_Yellow);
                } else {
                    LedPanel->Draw_P(LedPanel->m_Red);
                }
            }
            LedPanel->myShow();
            if (delayAndTestWirePluggedInFoil(100)) {
                break;
            }
            continue;
        }

        int arCr = testArCr();
        int arCl = testArCl();
        int arBr = testArBr();
        int brCr = testBrCr();

        // Case 1: Both ArBr and BrCr > 1500, ArCl > 600
        // No shorts -> Show E, ArCl > 600 means, no contact between tip and probe so measuring return wire
        if ((arBr > 1500 && brCr > 1500) && arCl > 600) {
            // Show color based on ArCr

            printf("Rac = %.1f\n", mycalibrator.get_resistance_empirical(arCr / 1000.0));
            if (arCr < myRefs_Ohm[2]) {
                tempColor = LedPanel->m_Green;
                // LedPanel->SetInner9(LedPanel->m_Green);
            } else if (arCr < myRefs_Ohm[4]) {
                // LedPanel->SetInner9(LedPanel->m_Yellow);
                tempColor = LedPanel->m_Yellow;
            } else if (arCr < 600) {
                // LedPanel->SetInner9(LedPanel->m_Orange);
                tempColor = LedPanel->m_Orange;
            } else {
                // Above 600, don't show anything
                if (SHAPE_E != ShowingShape) {
                    LedPanel->ClearAll();
                    ShowingShape = SHAPE_E;
                    LedPanel->Draw_E(LedPanel->m_White);
                }
                testWiresOnByOne();
                continue;
            }
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
            }
            LedPanel->SetInner9(tempColor);
            LedPanel->myShow();
            if (delayAndTestWirePluggedInEpee(1000)) {
                break;
            }
            // LedPanel->ClearAll();
            LedPanel->myShow();
        }
        // Case 2: Both ArBr and BrCr > 1500, ArCl < 600
        // No shorts -> Show E, ArCl < 600 means, contact between tip and probe so measuring single wire
        else if ((arBr > 1500 && brCr > 1500) && arCl < 600) {
            // Use thresholds divided by 2
            if (arCl < myRefs_Ohm[1]) {
                tempColor = LedPanel->m_Green;
            } else if (arCl < myRefs_Ohm[2]) {
                tempColor = LedPanel->m_Yellow;
            } else if (arCl < myRefs_Ohm[10]) {
                tempColor = LedPanel->m_Orange;
            } else {
                if (SHAPE_E != ShowingShape) {
                    LedPanel->ClearAll();
                    ShowingShape = SHAPE_E;
                    LedPanel->Draw_E(LedPanel->m_White);
                }
                testWiresOnByOne();
                continue;
            }
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
            }
            LedPanel->SetInner9(tempColor);
            LedPanel->myShow();
            if (delayAndTestWirePluggedInEpee(1000)) {
                break;
            }
            // LedPanel->ClearAll();
            LedPanel->myShow();
        }
        // Case 3: ArBr < 1500 or BrCr < 1500 (unwanted short)
        else if (arBr < 1500 || brCr < 1500) {
            LedPanel->ClearAll();
            if (arBr < 1500) {
                LedPanel->AnimateArBrConnection();
            }
            if (brCr < 1500) {
                LedPanel->AnimateBrCrConnection();
            }
        }
        // All other cases: draw white E
        else {
            if (SHAPE_E != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_E;
                LedPanel->Draw_E(LedPanel->m_White);
            }
        }

        esp_task_wdt_reset();
        testWiresOnByOne();
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
}

void Tester::doFoilTest() {
    testWiresOnByOne();
    int BrCl;

    while (!WirePluggedInFoil()) {
        esp_task_wdt_reset();
        LedPanel->myShow();
        BrCl = testBrCl();
        if (BrCl < 500) {
            if (SHAPE_P != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_P;
            }
            if (BrCl < myRefs_Ohm[5]) {
                LedPanel->Draw_P(LedPanel->m_Green);
            } else {
                if (BrCl < myRefs_Ohm[8]) {
                    LedPanel->Draw_P(LedPanel->m_Yellow);
                } else {
                    LedPanel->Draw_P(LedPanel->m_Orange);
                }
            }
            LedPanel->myShow();
            if (delayAndTestWirePluggedInFoil(100)) {
                Serial.println("Wire plugged in during foil light, breaking out");
                break;
            }
            continue;
        }

        int arBr = testArBr();
        if (SHAPE_F != ShowingShape) {
            LedPanel->ClearAll();
            ShowingShape = SHAPE_F;
        }
        if (arBr < myRefs_Ohm[2]) {
            LedPanel->Draw_F(LedPanel->m_Green);
            testWiresOnByOne();
            continue;
        } else if (arBr < myRefs_Ohm[4]) {
            LedPanel->Draw_F(LedPanel->m_Yellow);
            testWiresOnByOne();
            continue;
        } else if (arBr < 2000) {
            LedPanel->Draw_F(LedPanel->m_Orange);
            testWiresOnByOne();
            continue;
        } else {
            // Debounce: testArBr() > 1500 must be true for 10ms
            bool debounced = true;
            unsigned long start = millis();
            while (millis() - start < 10) {
                esp_task_wdt_reset();
                if (testArBr() <= 1500) {
                    debounced = false;
                    break;
                }
                taskYIELD();
            }

            if (!debounced) {
                Serial.println("Debouncing failed, continuing loop");
                continue;
            }

            // Debouncing succeeded: show inner lights based on ArCl
            if (SHAPE_SQUARE != ShowingShape) {
                LedPanel->ClearAll();
                ShowingShape = SHAPE_SQUARE;
            }
            if (testArCl() < myRefs_Ohm[1]) {
                LedPanel->SetInner9(LedPanel->m_Green);
            } else if (testArCl() < myRefs_Ohm[2]) {
                LedPanel->SetInner9(LedPanel->m_Yellow);
            } else if (testArCl() < 500) {
                LedPanel->SetInner9(LedPanel->m_Orange);
            } else {
                LedPanel->SetInner9(LedPanel->m_White);
            }
            LedPanel->myShow();

            if (delayAndTestWirePluggedInFoil(1000)) {
                Serial.println("Wire plugged in during foil light, breaking out");
                break;
            }
            if (testArBr() <= 1500) {
                LedPanel->ClearAll();
                LedPanel->myShow();
            }
        }

        testWiresOnByOne();
    }

    Serial.println("Wire plugged in during foil test, leaving");
    LedPanel->ClearAll();
    LedPanel->myShow();
}

void Tester::doLameTest() {
    // Your existing DoLameTest code
    bool bShowingRed = false;
    testWiresOnByOne();
    while (!WirePluggedIn()) {
        esp_task_wdt_reset();
        if (testBrCr() < myRefs_Ohm[5]) {
            LedPanel->DrawDiamond(LedPanel->m_Green);
            bShowingRed = false;
            // while((testBrCr()<myRefs_Ohm[5])){esp_task_wdt_reset();};
            while (debouncedCondition(
                [this]() {
                    int temp = testBrCr();
                    return temp < myRefs_Ohm[5];
                },
                10));
        } else {
            if (testBrCr() < myRefs_Ohm[10]) {
                LedPanel->DrawDiamond(LedPanel->m_Yellow);
                bShowingRed = false;
                // while((testBrCr()<myRefs_Ohm[10])){esp_task_wdt_reset();};
                while (debouncedCondition(
                    [this]() {
                        int temp = testBrCr();
                        return ((temp < myRefs_Ohm[10]) && (temp >= myRefs_Ohm[5]));
                    },
                    10));
            } else {
                if (testBrCr() < Ohm_25) {
                    LedPanel->DrawDiamond(LedPanel->m_Orange);
                    bShowingRed = false;
                    // while((testBrCr()<myRefs_Ohm[10])){esp_task_wdt_reset();};
                    while (debouncedCondition(
                        [this]() {
                            int temp = testBrCr();
                            return ((temp < myRefs_Ohm[25]) && (temp >= myRefs_Ohm[10]));
                        },
                        10));
                } else {
                    // Do Red stuff
                    bShowingRed = true;
                    LedPanel->DrawDiamond(LedPanel->m_Red);
                    if (delayAndTestWirePluggedIn(250))
                        break;
                }
            }
        }

        esp_task_wdt_reset();
        testWiresOnByOne();
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
}

bool DebounceTest(int LowBound, int HighBound) {
    testWiresOnByOne();
    if (WirePluggedInLameTopTesting()) {
        return false;
    }
    return ((testCrCl() >= LowBound) && (testCrCl() < HighBound));
}

void Tester::doLameTest_Top() {
    // Your existing DoLameTest code
    bool bShowingRed = false;
    testWiresOnByOne();
    while (!WirePluggedInLameTopTesting()) {
        esp_task_wdt_reset();
        if (testCrCl() < myRefs_Ohm[5]) {
            LedPanel->DrawDiamond(LedPanel->m_Green);
            bShowingRed = false;

            while (debouncedCondition([this]() { return DebounceTest(0, myRefs_Ohm[5]); }, 10));
        } else {
            if (testCrCl() < myRefs_Ohm[10]) {
                LedPanel->DrawDiamond(LedPanel->m_Yellow);
                bShowingRed = false;
                while (debouncedCondition([this]() { return DebounceTest(myRefs_Ohm[5], myRefs_Ohm[10]); }, 10));
            } else {
                if (testCrCl() < Ohm_25) {
                    LedPanel->DrawDiamond(LedPanel->m_Orange);
                    bShowingRed = false;
                    // while((testBrCr()<myRefs_Ohm[10])){esp_task_wdt_reset();};
                    while (debouncedCondition([this]() { return DebounceTest(myRefs_Ohm[10], Ohm_25); }, 10));
                } else {
                    LedPanel->DrawDiamond(LedPanel->m_Red);
                    bShowingRed = true;
                    if (delayAndTestWirePluggedInLameTestTop(250))
                        break;
                }
            }
        }

        esp_task_wdt_reset();
        testWiresOnByOne();
    }
    LedPanel->ClearAll();
    LedPanel->myShow();
}
void Tester::SetWiretestMode(bool Reelmode) {
    if (Reelmode) {
        ReferenceBroken = Ohm_50;
        ReferenceGreen = myRefs_Ohm[10];
        ReferenceYellow = Ohm_20;
        ReferenceOrange = Ohm_50;
        ReferenceShort = 300;
        ReelMode = true;
    } else {
        ReferenceBroken = myRefs_Ohm[10];
        ReferenceGreen = myRefs_Ohm[1];
        ReferenceYellow = myRefs_Ohm[3];
        ReferenceOrange = myRefs_Ohm[10];
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
