// Example usage of the Enhanced SettingsManager with Sections
// This file demonstrates how to use the new sectioned settings functionality

#include "src/SettingsManager.h"

// Example variables for settings
bool enableWiFi = true;
bool debugMode = false;
String deviceName = "ESP32Device";
String wifiSSID = "";
String wifiPassword = "";

int serverPort = 80;
int baudRate = 115200;
float calibrationFactor = 1.0;
float temperatureOffset = 0.0;

int thresholds[4] = {100, 200, 300, 400};
float coefficients[3] = {1.0, 0.5, 0.25};

void setupSettingsExample() {
    SettingsManager settings;

    // Initialize with namespace
    settings.begin("myapp");

    // === DEFINE SECTIONS ===

    // Basic settings section (shown first, expanded by default)
    settings.addSection("basic", "Basic Settings", 1, true, false);
    settings.setSectionDescription("basic", "Essential configuration options");

    // Advanced settings section (collapsible, starts collapsed)
    settings.addSection("advanced", "Advanced Settings", 2, true, true);
    settings.setSectionDescription("advanced", "Advanced configuration for experienced users");

    // Network subsection under advanced (collapsible, starts collapsed)
    settings.addSubsection("network", "Network Configuration", "advanced", 1, true, true);

    // Calibration subsection under advanced (collapsible, starts expanded)
    settings.addSubsection("calibration", "Calibration Settings", "advanced", 2, true, false);
    settings.setSectionDescription("calibration", "Fine-tune sensor accuracy and measurement parameters");

    // Diagnostics section (non-collapsible, always visible)
    settings.addSection("diagnostics", "Diagnostics", 3, false, false);

    // === ADD SETTINGS TO SECTIONS ===

    // Basic settings
    settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi, "basic");
    settings.setSettingHelp("enable_wifi", "Turn WiFi connectivity on or off");

    settings.addString("device_name", "Device Name", &deviceName, "basic");
    settings.setSettingHelp("device_name", "Friendly name for this device");

    // Network settings (subsection)
    settings.addString("wifi_ssid", "WiFi Network Name", &wifiSSID, "network");
    settings.addString("wifi_password", "WiFi Password", &wifiPassword, "network");
    settings.addInt("server_port", "Web Server Port", &serverPort, "network");
    settings.setSettingHelp("server_port", "Port number for the web interface (default: 80)");

    // Calibration settings (subsection)
    settings.addFloat("cal_factor", "Calibration Factor", &calibrationFactor, "calibration");
    settings.setSettingHelp("cal_factor", "Multiplier for sensor readings");

    settings.addFloat("temp_offset", "Temperature Offset", &temperatureOffset, "calibration");
    settings.setSettingHelp("temp_offset", "Temperature compensation in degrees Celsius");

    settings.addIntArray("thresholds", "Alert Thresholds", thresholds, 4, "calibration");
    settings.setSettingHelp("thresholds", "Threshold values for different alert levels");

    settings.addFloatArray("coefficients", "Correction Coefficients", coefficients, 3, "calibration");

    // Diagnostics settings
    settings.addBool("debug_mode", "Debug Mode", &debugMode, "diagnostics");
    settings.setSettingHelp("debug_mode", "Enable detailed logging for troubleshooting");

    settings.addInt("baud_rate", "Serial Baud Rate", &baudRate, "diagnostics");
    settings.setSettingReadonly("baud_rate", true);  // Make this read-only

    // Settings without section assignment go to default "General" section
    // This maintains backward compatibility
    // settings.addBool("some_legacy_setting", "Legacy Setting", &someBool);

    // Load existing values from preferences
    settings.load();

    // The web interface will now show:
    // 1. Basic Settings (expanded)
    //    - Enable WiFi
    //    - Device Name
    //
    // 2. Advanced Settings (collapsed initially)
    //    Network Configuration:
    //    - WiFi Network Name
    //    - WiFi Password
    //    - Web Server Port
    //
    //    Calibration Settings:
    //    - Calibration Factor
    //    - Temperature Offset
    //    - Alert Thresholds [0], [1], [2], [3]
    //    - Correction Coefficients [0], [1], [2]
    //
    // 3. Diagnostics (always expanded)
    //    - Debug Mode
    //    - Serial Baud Rate (read-only)
    //
    // 4. General (only if there are unsectioned settings)
    //    - Any settings added without section assignment
}

/*
MIGRATION GUIDE for existing code:

OLD CODE:
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi);
settings.addString("device_name", "Device Name", &deviceName);

NEW CODE (backward compatible):
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi);           // Goes to "General" section
settings.addString("device_name", "Device Name", &deviceName);         // Goes to "General" section

NEW CODE (with sections):
settings.addSection("basic", "Basic Settings");
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi, "basic");
settings.addString("device_name", "Device Name", &deviceName, "basic");

FEATURES AVAILABLE:
- Collapsible sections with expand/collapse
- Subsections for hierarchical organization
- Section descriptions and help text
- Read-only settings
- Automatic ordering by section order value
- Responsive design for mobile devices
- Backward compatibility with unsectioned settings
*/