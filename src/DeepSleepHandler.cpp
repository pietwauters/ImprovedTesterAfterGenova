#include "DeepSleepHandler.h"

#include "Arduino.h"
#include "driver/rtc_io.h"
#include "esp_task_wdt.h"
#include "soc/rtc_io_reg.h"

constexpr uint64_t BUTTON_PIN_BITMASK(int gpio) { return 1ULL << gpio; }
DeepSleepHandler::DeepSleepHandler() : sleepScheduledTime(0), sleepScheduled(false), timerWakeupEnabled(false) {}

bool DeepSleepHandler::isPinInHoldList(gpio_num_t pin) const {
    for (const auto& holdPin : holdPins) {
        if (holdPin.pin == pin)
            return true;
    }
    return false;
}

bool DeepSleepHandler::isPinInWakeupList(gpio_num_t pin) const {
    for (const auto& wakeupPin : wakeupPins) {
        if (wakeupPin.pin == pin)
            return true;
    }
    return false;
}

bool DeepSleepHandler::isGpioHoldCapable(gpio_num_t pin) {
    // Every output-capable pad can be held, RTC pads via the RTC hold and digital
    // pads via gpio_deep_sleep_hold_en(). GPIO6-11 are the SPI flash.
    return GPIO_IS_VALID_OUTPUT_GPIO(pin) && !(pin >= GPIO_NUM_6 && pin <= GPIO_NUM_11);
}

void DeepSleepHandler::addHoldPin(gpio_num_t pin, int value) {
    // Check for conflict with wake-up pins first
    if (isPinInWakeupList(pin)) {
        Serial.printf("WARNING: Pin %d is already configured as wake-up pin! This may cause conflicts.\n", pin);
    }

    // Check if pin supports GPIO hold (more restrictive than just RTC-capable)
    if (!isGpioHoldCapable(pin)) {
        Serial.printf("ERROR: Pin %d does NOT support GPIO hold during sleep!\n", pin);
        // return; // Do add non-hold-capable pins
    }

    // Check if pin already in hold list
    for (auto& holdPin : holdPins) {
        if (holdPin.pin == pin) {
            Serial.printf("Updating hold pin %d from value %d to %d\n", pin, holdPin.value, value);
            holdPin.value = value;
            return;
        }
    }

    // Add new hold pin
    holdPins.push_back({pin, value});
    Serial.printf("Added hold pin %d with value %d (GPIO hold capable: YES)\n", pin, value);
}

void DeepSleepHandler::addPulldownPin(gpio_num_t pin) {
    if (!rtc_gpio_is_valid_gpio(pin)) {
        Serial.printf("ERROR: Pin %d is not an RTC pin, its pull-down is lost in deep sleep!\n", pin);
    }
    for (auto p : pulldownPins) {
        if (p == pin)
            return;
    }
    pulldownPins.push_back(pin);
}

void DeepSleepHandler::addWakeupPin(gpio_num_t pin, WakeupTrigger trigger) {
    // Check for conflict with hold pins
    if (isPinInHoldList(pin)) {
        Serial.printf("WARNING: Pin %d is already configured as hold pin! This may cause conflicts.\n", pin);
    }

    // Check if pin already in wake-up list
    for (auto& wakeupPin : wakeupPins) {
        if (wakeupPin.pin == pin) {
            Serial.printf("Updating wake-up pin %d trigger\n", pin);
            wakeupPin.trigger = trigger;
            return;
        }
    }

    // Add new wake-up pin
    wakeupPins.push_back({pin, trigger});
    const char* triggerStr = "";
    switch (trigger) {
        case WakeupTrigger::WAKE_HIGH:
            triggerStr = "HIGH";
            break;
        case WakeupTrigger::WAKE_LOW:
            triggerStr = "LOW";
            break;
        case WakeupTrigger::WAKE_RISING:
            triggerStr = "RISING";
            break;
        case WakeupTrigger::WAKE_FALLING:
            triggerStr = "FALLING";
            break;
    }
    Serial.printf("Added wake-up pin %d with trigger %s\n", pin, triggerStr);
}

void DeepSleepHandler::setWakeupPins(const std::vector<WakeupPin>& pins) {
    wakeupPins.clear();
    for (const auto& pin : pins) {
        addWakeupPin(pin.pin, pin.trigger);
    }
}

void DeepSleepHandler::setWakeupBitmask(uint64_t bitmask, WakeupTrigger trigger) {
    wakeupPins.clear();
    for (int pin = 0; pin < 40; pin++) {
        if (bitmask & BUTTON_PIN_BITMASK(pin)) {
            addWakeupPin((gpio_num_t)pin, trigger);
        }
    }
}

uint64_t DeepSleepHandler::buildWakeupBitmask() const {
    uint64_t bitmask = 0;
    for (const auto& wakeupPin : wakeupPins) {
        bitmask |= BUTTON_PIN_BITMASK(wakeupPin.pin);
    }
    return bitmask;
}

void DeepSleepHandler::enableTimerWakeup(uint64_t timeInUs) {
    esp_sleep_enable_timer_wakeup(timeInUs);
    timerWakeupEnabled = true;
}

void DeepSleepHandler::disableTimerWakeup() {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    timerWakeupEnabled = false;
}

void DeepSleepHandler::scheduleDeepSleep(long delayMs) {
    sleepScheduledTime = millis() + delayMs;
    sleepScheduled = true;
}

void DeepSleepHandler::cancelScheduledSleep() { sleepScheduled = false; }

bool DeepSleepHandler::shouldSleepNow() const { return sleepScheduled && (millis() >= sleepScheduledTime); }

void DeepSleepHandler::addHighImpedancePin(gpio_num_t pin) {
    HighImpedancePin hiPin;
    hiPin.pin = pin;
    highImpedancePins.push_back(hiPin);

    Serial.printf("Added high impedance pin: GPIO %d\n", pin);
}

void DeepSleepHandler::enterDeepSleep() {
    // Only configure ext1 wakeup if there are wakeup pins configured
    if (!wakeupPins.empty()) {
        uint64_t bitmask = buildWakeupBitmask();
        // Use ext1 as a wake-up source
        esp_sleep_enable_ext1_wakeup(bitmask, ESP_EXT1_WAKEUP_ANY_HIGH);

        // Waarschijnlijk moet ik van de ADC pinnen eerst nog gewone IO pinnen maken
        for (const auto& wakeupPin : wakeupPins) {
            rtc_gpio_init(wakeupPin.pin);
            rtc_gpio_set_direction(wakeupPin.pin, RTC_GPIO_MODE_INPUT_ONLY);
            // Set pull-up or pull-down based on trigger type
            if (wakeupPin.trigger == WakeupTrigger::WAKE_HIGH || wakeupPin.trigger == WakeupTrigger::WAKE_RISING) {
                rtc_gpio_pullup_dis(wakeupPin.pin);
                rtc_gpio_pulldown_en(wakeupPin.pin);
            } else if (wakeupPin.trigger == WakeupTrigger::WAKE_LOW ||
                       wakeupPin.trigger == WakeupTrigger::WAKE_FALLING) {
                rtc_gpio_pulldown_dis(wakeupPin.pin);
                rtc_gpio_pullup_en(wakeupPin.pin);
            }
        }
    }
    // Drive the hold pins and pull down the pull-down pins, then latch all of them
    for (const auto& holdPin : holdPins) {
        gpio_set_direction(holdPin.pin, GPIO_MODE_OUTPUT);
        gpio_set_level(holdPin.pin, holdPin.value);
    }
    for (auto pin : pulldownPins) {
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_pullup_dis(pin);
        gpio_pulldown_en(pin);  // routed to the RTC pull-down on RTC pads
    }
    if (!pulldownPins.empty()) {
        // RTC pad pulls only stay active in deep sleep while RTC_PERIPH is powered
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    }
    if (!holdPins.empty() || !pulldownPins.empty()) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
        for (const auto& holdPin : holdPins) {
            if (gpio_hold_en(holdPin.pin) != ESP_OK) {
                Serial.printf("ERROR: GPIO hold failed for pin %d\n", holdPin.pin);
            }
        }
        for (auto pin : pulldownPins) {
            if (gpio_hold_en(pin) != ESP_OK) {
                Serial.printf("ERROR: GPIO hold failed for pin %d\n", pin);
            }
        }
        gpio_deep_sleep_hold_en();  // needed for the digital (non-RTC) pads
    }

    esp_task_wdt_deinit();  // <--- Add this line to disable the task WDT    Serial.println("Entering deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}

void DeepSleepHandler::handleWakeup() {
    Serial.println("Waking up from deep sleep...");

    releaseAllHolds();
    // Clear scheduled sleep
    sleepScheduled = false;
}

bool DeepSleepHandler::isWakeFromSleep() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    return (wakeup_reason != ESP_SLEEP_WAKEUP_UNDEFINED);
}

void DeepSleepHandler::releaseAllHolds() {
    gpio_deep_sleep_hold_dis();
    for (int pin = 0; pin < GPIO_NUM_MAX; pin++) {
        if (isGpioHoldCapable((gpio_num_t)pin)) {
            gpio_hold_dis((gpio_num_t)pin);
        }
    }
}

esp_sleep_wakeup_cause_t DeepSleepHandler::getWakeupCause() { return esp_sleep_get_wakeup_cause(); }

void DeepSleepHandler::printWakeupReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT1: {
            Serial.println("Wakeup caused by external signal using RTC_CNTL (EXT1)");
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();

            Serial.printf("Wake-up pin mask: 0x%llX\n", wakeup_pin_mask);

            if (wakeup_pin_mask != 0) {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                Serial.printf("Wake-up pin: %d\n", pin);

                // Debug: Show all pins that are HIGH
                Serial.println("All pin states at wake-up:");
                for (int p = 32; p <= 39; p++) {
                    if ((wakeup_pin_mask & (1ULL << p)) != 0) {
                        Serial.printf("  GPIO %d: HIGH (triggered wake-up)\n", p);
                    } else if (rtc_gpio_is_valid_gpio((gpio_num_t)p)) {
                        int state = gpio_get_level((gpio_num_t)p);
                        Serial.printf("  GPIO %d: %s\n", p, state ? "HIGH" : "LOW");
                    }
                }
            } else {
                Serial.println("ERROR: EXT1 wake-up but no pin mask!");
                Serial.println("This suggests the wake-up was caused by something else.");
            }
            break;
        }
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("Wakeup caused by external signal using RTC_IO (EXT0)");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("Wakeup caused by timer");
            break;
        default:
            Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
            break;
    }
}

void DeepSleepHandler::printConfiguration() const {
    Serial.println("=== DeepSleepHandler Configuration ===");
    Serial.println("Hold pins:");
    for (const auto& holdPin : holdPins) {
        Serial.printf("  Pin %d: %s\n", holdPin.pin, holdPin.value ? "HIGH" : "LOW");
    }
    Serial.println("Wake-up pins:");
    for (const auto& wakeupPin : wakeupPins) {
        const char* triggerStr = "";
        switch (wakeupPin.trigger) {
            case WakeupTrigger::WAKE_HIGH:
                triggerStr = "HIGH";
                break;
            case WakeupTrigger::WAKE_LOW:
                triggerStr = "LOW";
                break;
            case WakeupTrigger::WAKE_RISING:
                triggerStr = "RISING";
                break;
            case WakeupTrigger::WAKE_FALLING:
                triggerStr = "FALLING";
                break;
        }
        Serial.printf("  Pin %d: trigger on %s\n", wakeupPin.pin, triggerStr);
    }
}

void DeepSleepHandler::clearAllPins() {
    holdPins.clear();
    wakeupPins.clear();
    pulldownPins.clear();
    Serial.println("All pin configurations cleared");
}