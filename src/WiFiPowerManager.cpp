#include "WiFiPowerManager.h"

#include <ESPAsyncWebServer.h>

#include "esp_wifi.h"

// Singleton implementation
WiFiPowerManager& WiFiPowerManager::getInstance() {
    static WiFiPowerManager instance;
    return instance;
}

WiFiPowerManager::WiFiPowerManager()
    : lastActivityTime(0), wifiEnabled(false), autoManagementEnabled(true), initialized(false) {}

void WiFiPowerManager::begin() {
    // Ensure begin() is called only once
    if (initialized) {
        Serial.println("[WiFi PM] Warning: begin() already called, ignoring duplicate call");
        return;
    }

    initialized = true;
    Serial.println("[WiFi PM] Initializing WiFi Power Manager");

    // WiFi should already be initialized by main.cpp
    if (WiFi.getMode() != WIFI_OFF) {
        wifiEnabled = true;
        recordActivity();  // Start the timeout timer
        Serial.println("[WiFi PM] Started - WiFi is active");

        // Don't add automatic protection lock - let it timeout normally
        // The OTA detection will handle protection when needed
    } else {
        wifiEnabled = false;
        Serial.println("[WiFi PM] Started - WiFi is disabled");
    }
}

void WiFiPowerManager::loop() {
    if (!autoManagementEnabled || !wifiEnabled) {
        return;
    }

    // OTA detection is now handled by explicit callbacks
    // Remove the flawed heuristic detection

    // Check if we should disable WiFi
    if (shouldDisableWiFi()) {
        Serial.printf("[WiFi PM] Timeout reached (%d ms) and no active locks (%d) - disabling WiFi\n",
                      millis() - lastActivityTime, activeLocks.size());
        disableWiFi();
    }

    // Debug info every 10 seconds when close to timeout
    static uint32_t lastDebugTime = 0;
    uint32_t timeSinceActivity = millis() - lastActivityTime;
    if (timeSinceActivity > (WIFI_TIMEOUT_MS - 15000) && (millis() - lastDebugTime) > 10000) {
        Serial.printf("[WiFi PM] Debug: %d ms since activity, %d locks active\n", timeSinceActivity,
                      activeLocks.size());
        if (!activeLocks.empty()) {
            printActiveLocks();
        }
        lastDebugTime = millis();
    }
}

void WiFiPowerManager::recordActivity() {
    if (!wifiEnabled) {
        return;  // Can't record activity if WiFi is off
    }

    lastActivityTime = millis();
    Serial.println("[WiFi PM] Activity recorded - timeout reset");
}

void WiFiPowerManager::keepWiFiOn(const String& lockName) {
    if (activeLocks.find(lockName) == activeLocks.end()) {
        activeLocks.insert(lockName);
        Serial.printf("[WiFi PM] WiFi lock added: '%s' (total locks: %d)\n", lockName.c_str(), activeLocks.size());
    }

    // Record activity when a new lock is added
    if (wifiEnabled) {
        recordActivity();
    }
}

void WiFiPowerManager::releaseWiFiLock(const String& lockName) {
    auto it = activeLocks.find(lockName);
    if (it != activeLocks.end()) {
        activeLocks.erase(it);
        Serial.printf("[WiFi PM] WiFi lock released: '%s' (remaining locks: %d)\n", lockName.c_str(),
                      activeLocks.size());
    }
}

void WiFiPowerManager::disableWiFi() {
    if (!wifiEnabled)
        return;

    Serial.println("[WiFi PM] Disabling WiFi for power saving");
    Serial.println("[WiFi PM] WiFi can only be re-enabled by device reset");

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiEnabled = false;

    // Clear all locks since WiFi is now off
    activeLocks.clear();
}

bool WiFiPowerManager::shouldDisableWiFi() const {
    if (!wifiEnabled || !autoManagementEnabled) {
        return false;
    }

    // Don't disable if there are active locks
    if (hasActiveLocks()) {
        return false;
    }

    // Check timeout
    uint32_t timeSinceActivity = millis() - lastActivityTime;
    return (timeSinceActivity >= WIFI_TIMEOUT_MS);
}

// Explicit OTA callback methods - much more reliable than heuristics
void WiFiPowerManager::onOTAStart() {
    Serial.println("[WiFi PM] OTA Started - adding protection lock");
    keepWiFiOn("ota_active");
    recordActivity();
}

void WiFiPowerManager::onOTAEnd() {
    Serial.println("[WiFi PM] OTA Completed - releasing protection lock");
    releaseWiFiLock("ota_active");
}

void WiFiPowerManager::onOTAError() {
    Serial.println("[WiFi PM] OTA Error - releasing protection lock");
    releaseWiFiLock("ota_active");
}

uint32_t WiFiPowerManager::getSecondsUntilTimeout() const {
    if (!wifiEnabled || !autoManagementEnabled) {
        return 0;  // No timeout (WiFi off or auto-management disabled)
    }

    if (hasActiveLocks()) {
        return UINT32_MAX;  // Timeout indefinitely postponed due to active locks
    }

    uint32_t timeSinceActivity = millis() - lastActivityTime;
    if (timeSinceActivity >= WIFI_TIMEOUT_MS) {
        return 0;  // Already timed out
    }

    return (WIFI_TIMEOUT_MS - timeSinceActivity) / 1000;
}

void WiFiPowerManager::printStatus() const {
    Serial.println("[WiFi PM] === WiFi Power Manager Status ===");
    Serial.printf("  Initialized: %s\n", initialized ? "Yes" : "No");
    Serial.printf("  WiFi Active: %s\n", wifiEnabled ? "Yes" : "No");
    Serial.printf("  Auto Management: %s\n", autoManagementEnabled ? "Enabled" : "Disabled");
    Serial.printf("  Active Locks: %d\n", activeLocks.size());

    if (wifiEnabled && autoManagementEnabled) {
        uint32_t seconds = getSecondsUntilTimeout();
        if (seconds == UINT32_MAX) {
            Serial.println("  Timeout: Indefinitely postponed (active locks)");
        } else if (seconds > 0) {
            Serial.printf("  Time until timeout: %d seconds\n", seconds);
        } else {
            Serial.println("  Status: Ready to timeout");
        }
    }

    if (wifiEnabled) {
        Serial.printf("  AP IP: %s\n", WiFi.softAPIP().toString().c_str());

        wifi_sta_list_t stationList;
        if (esp_wifi_ap_get_sta_list(&stationList) == ESP_OK) {
            Serial.printf("  Connected clients: %d\n", stationList.num);
        }
    }
}

void WiFiPowerManager::printActiveLocks() const {
    if (activeLocks.empty()) {
        Serial.println("[WiFi PM] No active WiFi locks");
        return;
    }

    Serial.printf("[WiFi PM] Active WiFi locks (%d):\n", activeLocks.size());
    for (const auto& lock : activeLocks) {
        Serial.printf("  - %s\n", lock.c_str());
    }
}
