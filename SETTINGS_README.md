# Enhanced SettingsManager with Hierarchical Organization

The `SettingsManager` class now supports organizing settings into collapsible sections and subsections, providing a much more user-friendly web interface while maintaining complete backward compatibility.

## Features

### ✨ New Capabilities
- **Collapsible Sections**: Organize settings into expandable/collapsible groups
- **Collapsible Subsections**: Subsections also support expand/collapse functionality
- **Hierarchical Structure**: Support for subsections within main sections  
- **Visual Enhancement**: Modern, responsive web interface with better styling
- **Help Text**: Add contextual help for individual settings
- **Read-Only Settings**: Display-only settings that can't be modified
- **Automatic Ordering**: Control the display order of sections and settings
- **Mobile Responsive**: Works well on mobile devices and tablets

### 🔒 Backward Compatibility
- All existing `addXXX()` method calls work unchanged
- Settings without section assignment appear in a default "General" section
- No breaking changes to existing APIs

## Quick Start

```cpp
SettingsManager settings;

// Initialize
settings.begin("myapp");

// Define sections
settings.addSection("basic", "Basic Settings", 1);
settings.addSection("advanced", "Advanced Settings", 2, true, true); // collapsible, starts collapsed

// Add subsections
settings.addSubsection("network", "Network Configuration", "advanced");
settings.addSubsection("calibration", "Sensor Calibration", "advanced");

// Add settings to sections
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi, "basic");
settings.addString("device_name", "Device Name", &deviceName, "basic");
settings.addInt("server_port", "Server Port", &serverPort, "network");
settings.addFloat("cal_factor", "Calibration Factor", &calFactor, "calibration");

// Add help text and make settings read-only if needed
settings.setSettingHelp("cal_factor", "Multiplier applied to sensor readings");
settings.setSettingReadonly("server_port", true);

// Load and use
settings.load();
```

## API Reference

### Section Management

#### `addSection(id, title, order, collapsible, startCollapsed)`
Creates a new top-level section.

- `id`: Unique identifier for the section
- `title`: Display name shown in the web interface
- `order`: Display order (lower numbers appear first)
- `collapsible`: Whether users can collapse/expand this section
- `startCollapsed`: Initial state (true = starts collapsed)

```cpp
settings.addSection("basic", "Basic Settings", 1, true, false);     // Expandable, starts open
settings.addSection("advanced", "Advanced Settings", 2, true, true); // Expandable, starts collapsed
settings.addSection("status", "Status Information", 3, false);       // Always visible
```

#### `addSubsection(id, title, parentId, order, collapsible, startCollapsed)`
Creates a subsection within an existing section.

```cpp
settings.addSubsection("network", "Network Configuration", "advanced", 1, true, true);  // Collapsible, starts collapsed
settings.addSubsection("sensors", "Sensor Settings", "advanced", 2, true, false);      // Collapsible, starts expanded  
settings.addSubsection("status", "Status Display", "advanced", 3, false);              // Always visible
```

#### `setSectionDescription(sectionId, description)`
Adds explanatory text below the section title.

```cpp
settings.setSectionDescription("advanced", "These settings require technical knowledge");
```

### Enhanced Setting Registration

All existing `addXXX()` methods now accept an optional section parameter:

```cpp
// Original (still works) - goes to "General" section
settings.addBool("debug", "Debug Mode", &debugMode);

// New - assign to specific section
settings.addBool("debug", "Debug Mode", &debugMode, "advanced");
```

#### Available Methods:
- `addBool(key, label, valuePtr, sectionId)`
- `addInt(key, label, valuePtr, sectionId)`
- `addFloat(key, label, valuePtr, sectionId)`
- `addString(key, label, valuePtr, sectionId)`
- `addIntArray(key, label, arrayPtr, size, sectionId)`
- `addFloatArray(key, label, arrayPtr, size, sectionId)`

### Setting Enhancement

#### `setSettingHelp(key, helpText)`
Adds contextual help text displayed below the setting.

```cpp
settings.setSettingHelp("wifi_password", "Leave blank to use open network");
```

#### `setSettingReadonly(key, readonly)`
Makes a setting display-only (useful for status information).

```cpp
settings.setSettingReadonly("firmware_version", true);
```

## Web Interface Features

### Visual Layout

The generated web page now includes:

1. **Sectioned Organization**: Settings grouped logically
2. **Collapsible Sections**: Click headers to expand/collapse
3. **Visual Hierarchy**: Clear distinction between sections and subsections
4. **Help Text**: Contextual guidance for settings
5. **Responsive Design**: Works on desktop, tablet, and mobile
6. **Modern Styling**: Clean, professional appearance

### Section Behavior

- **Expandable Sections**: Show/hide content with smooth animation
- **Persistent State**: Section expand/collapse state maintained during page visit
- **Visual Indicators**: Clear arrows show expand/collapse state
- **Intuitive Layout**: Logical grouping reduces cognitive load

### Array Settings

Arrays are displayed in organized groups with clear indexing:

```cpp
int thresholds[4] = {100, 200, 300, 400};
settings.addIntArray("thresholds", "Alert Thresholds", thresholds, 4, "calibration");
```

Displays as:
```
Alert Thresholds
[0] [input field: 100]
[1] [input field: 200] 
[2] [input field: 300]
[3] [input field: 400]
```

## Migration Guide

### From Flat Settings

**Before (still works):**
```cpp
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi);
settings.addString("device_name", "Device Name", &deviceName);
settings.addInt("port", "Server Port", &port);
settings.addFloat("cal_factor", "Calibration Factor", &cal);
```

**After (organized):**
```cpp
// Define sections first
settings.addSection("basic", "Basic Settings");
settings.addSection("advanced", "Advanced Settings", 2, true, true);

// Assign settings to sections
settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi, "basic");
settings.addString("device_name", "Device Name", &deviceName, "basic");
settings.addInt("port", "Server Port", &port, "advanced");
settings.addFloat("cal_factor", "Calibration Factor", &cal, "advanced");

// Add help text for complex settings
settings.setSettingHelp("cal_factor", "Adjust this value based on sensor documentation");
```

### Gradual Migration

You can migrate gradually by:

1. **Phase 1**: Add sections without changing existing settings
2. **Phase 2**: Move settings to appropriate sections one by one
3. **Phase 3**: Add help text and other enhancements

## Best Practices

### Section Organization

1. **Basic First**: Put common settings in a "Basic" section that appears first
2. **Group Related**: Keep logically related settings together
3. **Advanced Last**: Put technical/dangerous settings in collapsible "Advanced" sections
4. **Status Separate**: Use non-collapsible sections for read-only status information

### Naming Conventions

- **Sections**: Use title case ("Basic Settings", "Network Configuration")
- **Settings**: Use descriptive labels ("Enable WiFi Module" vs "WiFi")
- **IDs**: Use snake_case for programmatic identifiers

### User Experience

1. **Start Collapsed**: Set advanced sections to start collapsed
2. **Add Help**: Provide help text for non-obvious settings
3. **Group Logically**: Keep related settings in the same section
4. **Read-Only Status**: Use read-only settings to display system information

## Example: Complete Configuration

```cpp
void setupSettings() {
    SettingsManager settings;
    settings.begin("device_config");
    
    // === SECTIONS ===
    settings.addSection("basic", "Basic Configuration", 1);
    settings.addSection("connectivity", "Connectivity", 2);  
    settings.addSection("advanced", "Advanced Settings", 3, true, true);
    settings.addSection("status", "System Status", 4, false);
    
    // Subsections
    settings.addSubsection("wifi", "WiFi Settings", "connectivity");
    settings.addSubsection("mqtt", "MQTT Settings", "connectivity");
    settings.addSubsection("calibration", "Sensor Calibration", "advanced");
    
    // === BASIC SETTINGS ===
    settings.addString("device_name", "Device Name", &deviceName, "basic");
    settings.setSettingHelp("device_name", "Friendly name for identification");
    
    settings.addBool("enable_led", "Status LED", &enableLED, "basic");
    
    // === CONNECTIVITY ===
    settings.addBool("enable_wifi", "Enable WiFi", &enableWiFi, "wifi");
    settings.addString("wifi_ssid", "Network Name", &wifiSSID, "wifi");
    settings.addString("wifi_password", "Password", &wifiPassword, "wifi");
    
    settings.addString("mqtt_server", "MQTT Broker", &mqttServer, "mqtt");
    settings.addInt("mqtt_port", "MQTT Port", &mqttPort, "mqtt");
    
    // === ADVANCED ===
    settings.addFloat("cal_factor", "Calibration Factor", &calFactor, "calibration");
    settings.setSettingHelp("cal_factor", "Sensor correction multiplier (1.0 = no correction)");
    
    settings.addIntArray("thresholds", "Alert Levels", thresholds, 4, "calibration");
    
    // === STATUS (READ-ONLY) ===
    settings.addString("firmware_version", "Firmware Version", &firmwareVersion, "status");
    settings.addString("ip_address", "IP Address", &ipAddress, "status"); 
    settings.addInt("uptime", "Uptime (seconds)", &uptime, "status");
    
    settings.setSettingReadonly("firmware_version", true);
    settings.setSettingReadonly("ip_address", true);
    settings.setSettingReadonly("uptime", true);
    
    settings.load();
}
```

This creates a well-organized settings interface with clear sections, helpful guidance, and appropriate access controls.