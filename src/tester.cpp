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

void Tester::begin() {
    mycalibrator.begin(br_analog, bl_analog);
    // Try to load existing calibration
    if (!mycalibrator.load_calibration_from_nvs()) {
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
        /*if (DoCalibration) {
            ledPanel->ClearAll();
            Calibrate();
        }*/

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
        vTaskDelay(10 / portTICK_PERIOD_MS);  // Small delay to prevent watchdog issues
    }
}

void Tester::handleWaitingState() {
    // Always update measurements first
    testWiresOnByOne();
    if (ReelMode) {
        if (ShowingShape != SHAPE_R) {
            LedPanel->ClearAll();
            LedPanel->Draw_R(LedPanel->m_Green);
            LedPanel->myShow();
            ShowingShape = SHAPE_R;
        }
        esp_task_wdt_reset();
        if (testAlBl() < Ohm_50) {
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

        if (testArCr() < Ohm_20) {
            currentState = EpeeTesting;
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doEpeeTest();
            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (testArBr() < Ohm_20) {
            ledPanel->ClearAll();
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doFoilTest();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (testBrCr() < Ohm_20) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doLameTest();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if ((testCrCl() < Ohm_20) && (measurements[1][1] > 160) && (measurements[2][2] > 160)) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance);
            doLameTest_Top();

            doCommonReturnFromSpecialMode();
            lastSpecialTestExit = millis();
        } else if (testAlBl() < Ohm_50) {
            UpdateThresholdsWithLeadResistance(AverageLeadResistance * 2);
            doReelTest();
        }

        esp_task_wdt_reset();
    }
    // Check for wire testing mode with delay after special tests
    if (WirePluggedIn(ReferenceBroken)) {
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
    testWiresOnByOne();
    allGood = doQuickCheck();

    if (allGood) {
        timeToSwitch--;
    } else {
        timeToSwitch = WIRE_TEST_1_TIMEOUT;
        if (!WirePluggedIn(ReferenceBroken)) {
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

        if (testStraightOnly(myRefs_Ohm[1])) {
            AverageLeadResistance = 0.0;
            for (int i = 0; i < 3; i++) {
                leadresistances[i] = mycalibrator.get_resistance_empirical(measurements[i][i] / 1000.0);
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
    testWiresOnByOne();
    while (WirePluggedIn(ReferenceBroken)) {
        for (int i = 100000; i > 0; i--) {
            esp_task_wdt_reset();
            if (!testStraightOnly(ReferenceBroken)) {
                i = 0;
            }
        }

        ledPanel->ClearAll();
        ledPanel->myShow();

        for (int i = 0; i < 3; i++) {
            allGood &= animateSingleWire(i);
        }

        esp_task_wdt_reset();
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
        ledPanel->ClearAll();
        ledPanel->myShow();
        doQuickCheck(false);  // check one more time (just to keep the correct colors)
        testWiresOnByOne();
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

bool Tester::animateSingleWire(int wireIndex, bool ReelMode) {
    // Your existing AnimateSingleWire code
    bool bOK = false;
    if (measurements[wireIndex][wireIndex] < ReferenceBroken) {
        if ((measurements[wireIndex][(wireIndex + 1) % 3] > 200) &&
            (measurements[wireIndex][(wireIndex + 2) % 3] > 200)) {
            // OK
            int level = 2;
            if (measurements[wireIndex][wireIndex] <= ReferenceYellow)
                level = 1;
            if (measurements[wireIndex][wireIndex] <= ReferenceGreen)
                level = 0;

            LedPanel->AnimateGoodConnection(wireIndex, level);
            bOK = true;
        } else {
            // short
            if (measurements[wireIndex][(wireIndex + 1) % 3] < ReferenceShort)
                LedPanel->AnimateShort(wireIndex, (wireIndex + 1) % 3);
            else if (measurements[wireIndex][(wireIndex + 2) % 3] < ReferenceShort)
                LedPanel->AnimateShort(wireIndex, (wireIndex + 2) % 3);
        }
    } else {
        if ((measurements[wireIndex][(wireIndex + 1) % 3] > ReferenceShort) &&
            (measurements[wireIndex][(wireIndex + 2) % 3] > ReferenceShort)) {
            // Simply broken
            LedPanel->AnimateBrokenConnection(wireIndex);
        } else {
            if (measurements[wireIndex][(wireIndex + 1) % 3] < ReferenceShort)
                LedPanel->AnimateWrongConnection(wireIndex, (wireIndex + 1) % 3);
            if (measurements[wireIndex][(wireIndex + 2) % 3] < ReferenceShort)
                LedPanel->AnimateWrongConnection(wireIndex, (wireIndex + 2) % 3);
        }
    }
    return bOK;
}

bool Tester::doQuickCheck(bool bClearAtTheEnd) {
    // Your existing DoQuickCheck code
    bool bAllGood = true;
    testWiresOnByOne();
    for (int i = 0; i < 3; i++) {
        bAllGood &= animateSingleWire(i);
    }
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
