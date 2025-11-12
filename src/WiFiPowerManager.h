#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include <set>

class WiFiPowerManager {
   private:
    static const uint32_t WIFI_TIMEOUT_MS = 90000;  // 1 minute timeout

    uint32_t lastActivityTime;
    bool wifiEnabled;
    bool autoManagementEnabled;
    bool initialized;              // Flag to ensure begin() is called only once
    std::set<String> activeLocks;  // Reference counting for WiFi locks

    // Private constructor for singleton
    WiFiPowerManager();

    // Delete copy constructor and assignment operator
    WiFiPowerManager(const WiFiPowerManager&) = delete;
    WiFiPowerManager& operator=(const WiFiPowerManager&) = delete;

    // Private methods
    void disableWiFi();

   public:
    // Singleton getInstance method
    static WiFiPowerManager& getInstance();

    // Core management
    void begin();
    void loop();
    void recordActivity();

    // Reference counting system
    void keepWiFiOn(const String& lockName);
    void releaseWiFiLock(const String& lockName);
    bool hasActiveLocks() const { return !activeLocks.empty(); }

    // Control methods
    void setAutoManagement(bool enabled) { autoManagementEnabled = enabled; }
    void setTimeout(uint32_t timeoutMs) { /* Only allow setting before begin() */ }

    // Status methods
    bool isWiFiActive() const { return wifiEnabled; }
    bool isInitialized() const { return initialized; }
    uint32_t getSecondsUntilTimeout() const;
    size_t getActiveLockCount() const { return activeLocks.size(); }

    // OTA management
    void onOTAStart();
    void onOTAEnd();
    void onOTAError();

    // Debug methods
    void printStatus() const;
    void printActiveLocks() const;

   private:
    bool shouldDisableWiFi() const;
};

// Global access helper function
inline WiFiPowerManager& wifiPowerManager() { return WiFiPowerManager::getInstance(); }
