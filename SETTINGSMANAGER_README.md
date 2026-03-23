# SettingsManager — Usage Guide

`SettingsManager` provides a persistent settings system for ESP32 projects. It stores values in NVS (Non-Volatile Storage) via the Arduino `Preferences` library and automatically generates a web UI at `/settings` served by `ESPAsyncWebServer`.

---

## Quick Overview

| Feature | Description |
|---|---|
| Supported types | `bool`, `int`, `float`, `String`, `int[]`, `float[]` |
| Storage backend | ESP32 NVS via `Preferences` |
| Web UI | Auto-generated HTML form at `/settings` |
| Sections | Collapsible groups; supports parent/child (subsections) |
| Buttons | Action buttons with server-side callbacks |
| Save callback | Hook called after the web form is saved |

---

## 1. Declare Your Variables

Settings are stored in normal C++ variables. `SettingsManager` keeps pointers to them, so they must remain valid for the lifetime of the manager (global or static scope is typical).

```cpp
// globals.h or main.cpp

bool  wifiEnabled   = false;
int   sampleRate    = 100;
float threshold     = 2.5f;
String deviceName   = "Tester-01";

int   calOffsets[4] = {0, 0, 0, 0};   // int array example
float calGains[4]   = {1.0f, 1.0f, 1.0f, 1.0f};  // float array example

SettingsManager settingsManager;
```

---

## 2. Initialise and Register Settings

Call this in `setup()` **before** registering web endpoints.

```cpp
#include "SettingsManager.h"

void setup() {
    // 1. Open the NVS namespace (max 15 chars)
    settingsManager.begin("myapp");

    // 2. (Optional) Create collapsible sections for grouping
    settingsManager.addSection("network",  "Network",      0);
    settingsManager.addSection("hardware", "Hardware",     1);
    settingsManager.addSection("advanced", "Advanced",     2, true, true); // starts collapsed

    // optionally add a subsection inside "hardware"
    settingsManager.addSubsection("calibration", "Calibration", "hardware", 0);

    // 3. Register settings — each one binds a key, a UI label, and a variable
    settingsManager.addBool  ("wifi_en",     "Enable WiFi",     &wifiEnabled,  "network");
    settingsManager.addString("dev_name",    "Device Name",     &deviceName,   "network");

    settingsManager.addInt   ("sample_rate", "Sample Rate (ms)", &sampleRate,  "hardware");
    settingsManager.addFloat ("threshold",   "Threshold (V)",    &threshold,   "hardware");

    settingsManager.addIntArray  ("cal_off",  "Cal Offsets", calOffsets, 4, "calibration");
    settingsManager.addFloatArray("cal_gain", "Cal Gains",   calGains,   4, "calibration");

    // 4. (Optional) Add help text and read-only flags
    settingsManager.setSettingHelp("threshold", "Trigger level in volts (0.0 – 5.0)");
    settingsManager.setSettingReadonly("dev_name", false);

    // 5. Load previously saved values from NVS
    settingsManager.load();

    // 6. Register web endpoints (call after WiFi / server init)
    settingsManager.addWebEndpoints(server);
    server.begin();
}
```

### Key Rules
- Keys must be **≤ 15 characters** for direct NVS storage; longer keys are automatically hashed — but keep them short and descriptive to be safe.
- The variable pointer must stay valid. Prefer globals or static locals.
- `load()` overwrites the variables with whatever is in NVS. If a key has never been saved, the variable keeps its initialised default value.

---

## 3. Using the Values Elsewhere in the Program

After `load()` runs, the variables contain the stored values. Just read them directly — no getter needed.

```cpp
void loop() {
    if (wifiEnabled) {
        connectWiFi();
    }

    delay(sampleRate);   // uses the stored int directly

    if (readVoltage() > threshold) {
        triggerAlarm();
    }

    // Array access is the same as any C array
    for (int i = 0; i < 4; i++) {
        applyCalibration(i, calOffsets[i], calGains[i]);
    }
}
```

Because `SettingsManager` stores a **pointer** to each variable, any change made through the web UI (followed by `save()`) is immediately reflected in that same variable — no extra sync step required.

---

## 4. Save Callback

Register a callback to be called immediately after the web form is submitted and saved. Useful for applying changes (e.g. restarting a service, re-initialising hardware).

```cpp
settingsManager.setPostSaveCallback([]() {
    Serial.println("Settings saved — applying changes...");
    applyNewSampleRate(sampleRate);
    if (wifiEnabled) reconnectWiFi();
});
```

---

## 5. Action Buttons

Buttons appear in the web UI and fire a server-side callback when clicked. They are **independent** of the Save form.

```cpp
settingsManager.addButton(
    "reset_fie",     // unique id  →  POST /settings/btn_reset_fie
    "Reset to FIE",  // label shown in the browser
    []() {
        // reset logic runs on the ESP32 when the button is clicked
        applyFIEDefaults();
        settingsManager.save();
        Serial.println("Reset to FIE defaults applied.");
    },
    "advanced"       // optional: which section to render in
);
```

Multiple buttons are supported; each gets its own endpoint.

---

## 6. Sections

Sections create collapsible groups in the UI. They are purely cosmetic — they do not affect storage.

```cpp
// Top-level section
settingsManager.addSection("network", "Network Settings", /*order=*/0);

// Add a description and icon hint
settingsManager.setSectionDescription("network", "WiFi and connectivity options");
settingsManager.setSectionIcon("network", "fa-wifi");  // for custom CSS

// Subsection nested inside "network"
settingsManager.addSubsection("mqtt", "MQTT", /*parentId=*/"network", /*order=*/0);

// Then register settings into the subsection
settingsManager.addString("mqtt_host", "MQTT Broker", &mqttHost, "mqtt");
settingsManager.addInt   ("mqtt_port", "MQTT Port",   &mqttPort, "mqtt");
```

Settings with no `sectionId` (or `nullptr`) are placed in an auto-created **"General Settings"** section.

---

## 7. Checking Whether a Value Was Ever Saved

```cpp
// Check by raw NVS key
if (settingsManager.keyExists("sample_rate")) {
    Serial.println("sample_rate is in NVS");
}

// Check by registered setting key (handles arrays correctly)
if (!settingsManager.settingExists("cal_off")) {
    Serial.println("Calibration not yet saved — using defaults");
}
```

---

## 8. Complete Minimal Example

```cpp
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SettingsManager.h"

AsyncWebServer server(80);
SettingsManager settingsManager;

// --- application variables ---
bool  ledEnabled = true;
int   brightness = 128;
float targetTemp = 22.5f;
String ssid      = "MyNetwork";

void setup() {
    Serial.begin(115200);

    settingsManager.begin("app");

    settingsManager.addSection("display", "Display", 0);
    settingsManager.addSection("wifi",    "WiFi",    1);

    settingsManager.addBool  ("led_en",  "LED Enabled",  &ledEnabled, "display");
    settingsManager.addInt   ("bright",  "Brightness",   &brightness, "display");
    settingsManager.addFloat ("temp",    "Target Temp",  &targetTemp, "display");
    settingsManager.addString("ssid",    "SSID",         &ssid,       "wifi");

    settingsManager.setSettingHelp("bright", "0–255");

    settingsManager.setPostSaveCallback([]() {
        analogWrite(LED_PIN, ledEnabled ? brightness : 0);
    });

    settingsManager.addButton("factory", "Factory Reset", []() {
        ledEnabled = true;
        brightness = 128;
        targetTemp = 22.5f;
        ssid       = "MyNetwork";
        settingsManager.save();
    }, "wifi");

    settingsManager.load();

    WiFi.begin(ssid.c_str(), "password");
    while (WiFi.status() != WL_CONNECTED) delay(500);

    settingsManager.addWebEndpoints(server);
    server.begin();

    // Apply loaded values immediately
    analogWrite(LED_PIN, ledEnabled ? brightness : 0);
}

void loop() {
    // Variables are always up-to-date — just use them
    if (readTemp() > targetTemp) {
        activateCooling();
    }
    delay(100);
}
```

---

## 9. API Reference Summary

```cpp
// Initialisation
void begin(const String& ns);          // open NVS namespace
void load();                           // load all values from NVS
void save();                           // save all values to NVS
void addWebEndpoints(AsyncWebServer&); // register /settings GET + POST

// Sections
void addSection   (id, title, order, collapsible, startCollapsed);
void addSubsection(id, title, parentId, order, collapsible, startCollapsed);
void setSectionDescription(sectionId, description);
void setSectionIcon       (sectionId, iconClass);

// Settings
void addBool      (key, label, bool*,   sectionId);
void addInt       (key, label, int*,    sectionId);
void addFloat     (key, label, float*,  sectionId);
void addString    (key, label, String*, sectionId);
void addIntArray  (key, label, int*,   size, sectionId);
void addFloatArray(key, label, float*, size, sectionId);

void setSettingHelp    (key, helpText);   // tooltip in the UI
void setSettingReadonly(key, bool);       // display-only (not editable)

// Buttons
void addButton(id, label, std::function<void()> callback, sectionId);

// Callbacks
void setPostSaveCallback(std::function<void()>);

// NVS inspection
bool keyExists    (key);   // raw NVS key check
bool settingExists(key);   // registered-setting check (handles arrays)
```
