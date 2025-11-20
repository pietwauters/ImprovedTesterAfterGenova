#include "SettingsManager.h"

#include <algorithm>

#include "version.h"

static uint32_t hashKey(const String& key) {
    uint32_t hash = 5381;
    for (int i = 0; i < key.length(); i++) {
        hash = ((hash << 5) + hash) + key[i];  // hash * 33 + c
    }
    return hash;
}
String SettingsManager::getPrefKey(const String& key) {
    const size_t MAX_KEY_LENGTH = 15;
    if (key.length() <= MAX_KEY_LENGTH) {
        return key;  // use original key if short
    } else {
        // prefix with k_ to avoid purely numeric keys, convert to uppercase hex
        char buf[11];
        snprintf(buf, sizeof(buf), "k_%08X", hashKey(key));
        return String(buf);
    }
}

void SettingsManager::addBool(const char* key, const char* label, bool* value, const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), BOOL, value, 0, section});
}

void SettingsManager::addInt(const char* key, const char* label, int* value, const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), INT, value, 0, section});
}

void SettingsManager::addFloat(const char* key, const char* label, float* value, const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), FLOAT, value, 0, section});
}

void SettingsManager::addString(const char* key, const char* label, String* value, const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), STRING, value, 0, section});
}

void SettingsManager::addIntArray(const char* key, const char* label, int* array, size_t size, const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), ARRAY_INT, array, size, section});
}

void SettingsManager::addFloatArray(const char* key, const char* label, float* array, size_t size,
                                    const char* sectionId) {
    String section = sectionId ? String(sectionId) : String("");
    settings.push_back({String(key), String(label), ARRAY_FLOAT, array, size, section});
}

void SettingsManager::begin(const String& ns) {
    namespaceStr = ns;
    prefs.begin(namespaceStr.c_str(), false);
}

void SettingsManager::load() {
    prefs.begin(namespaceStr.c_str(), false);
    for (auto& s : settings) {
        String key = getPrefKey(s.key);

        if (s.type == BOOL)
            *(bool*)s.value = prefs.getBool(key.c_str(), false);
        else if (s.type == INT)
            *(int*)s.value = prefs.getInt(key.c_str(), 0);
        else if (s.type == FLOAT)
            *(float*)s.value = prefs.getFloat(key.c_str(), 0.0f);
        else if (s.type == STRING)
            *(String*)s.value = prefs.getString(key.c_str(), "");

        else if (s.type == ARRAY_INT) {
            int* arr = (int*)s.value;
            for (size_t i = 0; i < s.size; i++) {
                String indexedKey = getPrefKey(s.key + "_" + String(i));
                arr[i] = prefs.getInt(indexedKey.c_str(), 0);
            }
        } else if (s.type == ARRAY_FLOAT) {
            float* arr = (float*)s.value;
            for (size_t i = 0; i < s.size; i++) {
                String indexedKey = getPrefKey(s.key + "_" + String(i));
                arr[i] = prefs.getFloat(indexedKey.c_str(), 0.0f);
            }
        }
    }
    prefs.end();
}

void SettingsManager::save() {
    prefs.begin(namespaceStr.c_str(), false);
    for (auto& s : settings) {
        String key = getPrefKey(s.key);

        if (s.type == BOOL)
            prefs.putBool(key.c_str(), *(bool*)s.value);
        else if (s.type == INT)
            prefs.putInt(key.c_str(), *(int*)s.value);
        else if (s.type == FLOAT)
            prefs.putFloat(key.c_str(), *(float*)s.value);
        else if (s.type == STRING)
            prefs.putString(key.c_str(), *(String*)s.value);

        else if (s.type == ARRAY_INT) {
            int* arr = (int*)s.value;
            for (size_t i = 0; i < s.size; i++) {
                String indexedKey = getPrefKey(s.key + "_" + String(i));
                prefs.putInt(indexedKey.c_str(), arr[i]);
            }
        } else if (s.type == ARRAY_FLOAT) {
            float* arr = (float*)s.value;
            for (size_t i = 0; i < s.size; i++) {
                String indexedKey = getPrefKey(s.key + "_" + String(i));
                prefs.putFloat(indexedKey.c_str(), arr[i]);
            }
        }
    }
    prefs.end();
    // ESP.restart();
}

bool SettingsManager::keyExists(const String& key) {
    prefs.begin(namespaceStr.c_str(), true);  // Read-only mode
    bool exists = prefs.isKey(key.c_str());
    prefs.end();
    return exists;
}

bool SettingsManager::keyExists(const char* key) { return keyExists(String(key)); }

bool SettingsManager::settingExists(const String& settingKey) {
    // First, check if this is a registered setting
    bool isRegisteredSetting = false;
    SettingType settingType = BOOL;  // Default value
    size_t settingSize = 0;

    for (const auto& s : settings) {
        if (s.key == settingKey) {
            isRegisteredSetting = true;
            settingType = s.type;
            settingSize = s.size;
            break;
        }
    }

    if (!isRegisteredSetting) {
        // Not a registered setting, just check if the key exists
        return keyExists(getPrefKey(settingKey));
    }

    // For registered settings, check based on type
    if (settingType == ARRAY_INT || settingType == ARRAY_FLOAT) {
        // For arrays, check if all elements exist
        for (size_t i = 0; i < settingSize; i++) {
            String indexedKey = getPrefKey(settingKey + "_" + String(i));
            if (!keyExists(indexedKey)) {
                return false;
            }
        }
        return true;
    } else {
        // For non-array types, check the single key
        return keyExists(getPrefKey(settingKey));
    }
}

bool SettingsManager::settingExists(const char* settingKey) { return settingExists(String(settingKey)); }

uint32_t SettingsManager::fnv1aHash(const String& str) {
    const uint32_t FNV_PRIME = 0x01000193;
    uint32_t hash = 0x811c9dc5;

    for (size_t i = 0; i < str.length(); i++) {
        hash ^= (uint8_t)str[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

String SettingsManager::makeHashedKey(const String& base, size_t index) {
    String full = base + "_" + String(index);
    uint32_t hash = fnv1aHash(full);
    char buf[16];
    snprintf(buf, sizeof(buf), "k_%08X", hash);
    return String(buf);
}

// For completeness: you should implement setupWeb() and generateHTML() to create your webpage,
// including form elements for all your settings, mapping to their keys for get/post handling.
// This code focuses on storage and key handling as requested.

void SettingsManager::setPostSaveCallback(std::function<void()> callback) { postSaveCallback = callback; }

// Section Management Methods
void SettingsManager::addSection(const char* id, const char* title, int order, bool collapsible, bool startCollapsed) {
    SettingGroup section(String(id), String(title), order, collapsible, startCollapsed);
    sections.push_back(section);
}

void SettingsManager::addSubsection(const char* id, const char* title, const char* parentId, int order,
                                    bool collapsible, bool startCollapsed) {
    SettingGroup subsection(String(id), String(title), order, collapsible, startCollapsed);
    subsection.parentId = String(parentId);
    sections.push_back(subsection);
}

void SettingsManager::setSectionDescription(const char* sectionId, const char* description) {
    SettingGroup* section = findSection(String(sectionId));
    if (section) {
        section->description = String(description);
    }
}

void SettingsManager::setSectionIcon(const char* sectionId, const char* iconClass) {
    SettingGroup* section = findSection(String(sectionId));
    if (section) {
        section->iconClass = String(iconClass);
    }
}

void SettingsManager::setSettingHelp(const char* key, const char* helpText) {
    for (auto& s : settings) {
        if (s.key == String(key)) {
            s.helpText = String(helpText);
            break;
        }
    }
}

void SettingsManager::setSettingReadonly(const char* key, bool readonly) {
    for (auto& s : settings) {
        if (s.key == String(key)) {
            s.readonly = readonly;
            break;
        }
    }
}

// Helper Methods
SettingsManager::SettingGroup* SettingsManager::findSection(const String& id) {
    for (auto& section : sections) {
        if (section.id == id) {
            return &section;
        }
    }
    return nullptr;
}

std::vector<SettingsManager::SettingGroup> SettingsManager::getSortedSections() const {
    std::vector<SettingGroup> sorted = sections;
    std::sort(sorted.begin(), sorted.end(),
              [](const SettingGroup& a, const SettingGroup& b) { return a.order < b.order; });
    return sorted;
}

std::vector<SettingsManager::Setting> SettingsManager::getSettingsForSection(const String& sectionId) const {
    std::vector<Setting> sectionSettings;
    for (const auto& s : settings) {
        if (s.sectionId == sectionId) {
            sectionSettings.push_back(s);
        }
    }
    return sectionSettings;
}

void SettingsManager::ensureDefaultSection() {
    // Check if we have any settings without a section
    bool hasUnsectionedSettings = false;
    for (const auto& s : settings) {
        if (s.sectionId.isEmpty()) {
            hasUnsectionedSettings = true;
            break;
        }
    }

    // Add default section if needed and it doesn't exist
    if (hasUnsectionedSettings && findSection("general") == nullptr) {
        SettingGroup defaultSection("general", "General Settings", 0, true, false);
        sections.push_back(defaultSection);
    }
}

String SettingsManager::generateSettingHTML(const Setting& s) {
    String html = "";
    String readonlyAttr = s.readonly ? " readonly" : "";

    if (s.type == BOOL) {
        bool val = *(bool*)s.value;
        html += "<div class='setting-item checkbox-item'>";
        html += "<input type='checkbox' name='" + s.key + "'";
        if (val)
            html += " checked";
        if (s.readonly)
            html += " disabled";
        html += ">";
        html += "<span class='setting-label'>" + s.label + "</span>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        html += "</div>";

    } else if (s.type == INT) {
        int val = *(int*)s.value;
        html += "<div class='setting-item'>";
        html += "<span class='setting-label'>" + s.label + "</span>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        html += "<input type='number' name='" + s.key + "' value='" + String(val) + "'" + readonlyAttr + ">";
        html += "</div>";

    } else if (s.type == FLOAT) {
        float val = *(float*)s.value;
        html += "<div class='setting-item'>";
        html += "<span class='setting-label'>" + s.label + "</span>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        html += "<input type='number' step='any' name='" + s.key + "' value='" + String(val) + "'" + readonlyAttr + ">";
        html += "</div>";

    } else if (s.type == STRING) {
        String val = *(String*)s.value;
        html += "<div class='setting-item'>";
        html += "<span class='setting-label'>" + s.label + "</span>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        html += "<input type='text' name='" + s.key + "' value='" + val + "'" + readonlyAttr + ">";
        html += "</div>";

    } else if (s.type == ARRAY_INT) {
        int* arr = (int*)s.value;
        html += "<div class='setting-item'>";
        html += "<div class='array-setting'>";
        html += "<div class='array-title'>" + s.label + "</div>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        for (size_t i = 0; i < s.size; i++) {
            String indexedKey = s.key + "_" + String(i);
            html += "<div class='array-item'>";
            html += "<span class='array-index'>[" + String(i) + "]</span>";
            html +=
                "<input type='number' name='" + indexedKey + "' value='" + String(arr[i]) + "'" + readonlyAttr + ">";
            html += "</div>";
        }
        html += "</div></div>";

    } else if (s.type == ARRAY_FLOAT) {
        float* arr = (float*)s.value;
        html += "<div class='setting-item'>";
        html += "<div class='array-setting'>";
        html += "<div class='array-title'>" + s.label + "</div>";
        if (!s.helpText.isEmpty()) {
            html += "<div class='setting-help'>" + s.helpText + "</div>";
        }
        for (size_t i = 0; i < s.size; i++) {
            String indexedKey = s.key + "_" + String(i);
            html += "<div class='array-item'>";
            html += "<span class='array-index'>[" + String(i) + "]</span>";
            html += "<input type='number' step='any' name='" + indexedKey + "' value='" + String(arr[i]) + "'" +
                    readonlyAttr + ">";
            html += "</div>";
        }
        html += "</div></div>";
    }

    return html;
}

void SettingsManager::addWebEndpoints(AsyncWebServer& server) {
    server.on("/settings", HTTP_GET, [this](AsyncWebServerRequest* request) {
        // Ensure we have a default section for unsectioned settings
        ensureDefaultSection();

        String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <title>ESP32 Settings</title>
      <style>
        body {
          font-family: sans-serif;
          margin: 20px;
          max-width: 800px;
          width: 100%;
          background-color: #f5f5f5;
        }
        .container {
          background-color: white;
          padding: 20px;
          border-radius: 8px;
          box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        h1 { color: #333; margin-bottom: 20px; }
        .version { color: #666; font-size: 0.9em; margin-bottom: 20px; }
        
        .section {
          border: 1px solid #ddd;
          border-radius: 6px;
          margin-bottom: 15px;
          overflow: hidden;
        }
        
        .section-header {
          background-color: #f8f9fa;
          padding: 12px 15px;
          border-bottom: 1px solid #ddd;
          cursor: pointer;
          display: flex;
          justify-content: space-between;
          align-items: center;
          user-select: none;
        }
        
        .section-header:hover {
          background-color: #e9ecef;
        }
        
        .section-title {
          font-weight: bold;
          color: #333;
        }
        
        .section-description {
          font-size: 0.9em;
          color: #666;
          margin-top: 4px;
        }
        
        .section-toggle {
          font-size: 1.2em;
          color: #666;
          transition: transform 0.2s;
        }
        
        .section-content {
          padding: 15px;
          display: block;
        }
        
        .section-content.collapsed {
          display: none;
        }
        
        .subsection {
          border-left: 3px solid #007bff;
          margin: 10px 0;
          background-color: #f8f9fa;
          border-radius: 4px;
          overflow: hidden;
        }
        
        .subsection-header {
          background-color: #e9ecef;
          padding: 10px 15px;
          cursor: pointer;
          display: flex;
          justify-content: space-between;
          align-items: center;
          user-select: none;
          border-left: 3px solid #007bff;
          margin-left: -3px;
        }
        
        .subsection-header:hover {
          background-color: #dee2e6;
        }
        
        .subsection-title {
          font-weight: 600;
          color: #495057;
          font-size: 1.05em;
        }
        
        .subsection-toggle {
          font-size: 1em;
          color: #666;
          transition: transform 0.2s;
        }
        
        .subsection-content {
          padding: 15px;
          display: block;
          background-color: white;
        }
        
        .subsection-content.collapsed {
          display: none;
        }
        
        .form-group {
          display: flex;
          flex-direction: column;
          gap: 12px;
          margin: 0;
        }
        
        .setting-item {
          display: flex;
          flex-direction: column;
          gap: 4px;
          padding: 8px 0;
        }
        
        .setting-label {
          font-weight: 500;
          color: #333;
          font-size: 0.95em;
        }
        
        .setting-help {
          font-size: 0.85em;
          color: #666;
          font-style: italic;
        }
        
        .checkbox-item {
          flex-direction: row;
          align-items: center;
          gap: 8px;
        }
        
        input[type="text"],
        input[type="number"] {
          font-size: 1rem;
          padding: 8px 12px;
          border: 1px solid #ced4da;
          border-radius: 4px;
          width: 100%;
          box-sizing: border-box;
        }
        
        input[type="text"]:focus,
        input[type="number"]:focus {
          outline: none;
          border-color: #80bdff;
          box-shadow: 0 0 0 2px rgba(0,123,255,0.25);
        }
        
        input[type="checkbox"] {
          width: 18px;
          height: 18px;
          accent-color: #007bff;
        }
        
        input[type="submit"] {
          padding: 12px 24px;
          font-size: 1rem;
          background-color: #007bff;
          color: white;
          border: none;
          border-radius: 4px;
          cursor: pointer;
          margin-top: 20px;
          transition: background-color 0.2s;
        }
        
        input[type="submit"]:hover {
          background-color: #0056b3;
        }
        
        input[readonly] {
          background-color: #f8f9fa;
          color: #6c757d;
        }
        
        .array-setting {
          border: 1px solid #e9ecef;
          border-radius: 4px;
          padding: 10px;
          background-color: #f8f9fa;
        }
        
        .array-title {
          font-weight: 500;
          margin-bottom: 8px;
          color: #495057;
        }
        
        .array-item {
          display: flex;
          align-items: center;
          gap: 8px;
          margin-bottom: 6px;
        }
        
        .array-index {
          min-width: 40px;
          font-size: 0.9em;
          color: #666;
        }
      </style>
      <script>
        function toggleSection(sectionId) {
          const content = document.getElementById('content-' + sectionId);
          const toggle = document.getElementById('toggle-' + sectionId);
          
          if (content.classList.contains('collapsed')) {
            content.classList.remove('collapsed');
            toggle.textContent = '▼';
          } else {
            content.classList.add('collapsed');
            toggle.textContent = '▶';
          }
        }
        
        function toggleSubsection(subsectionId) {
          const content = document.getElementById('subcontent-' + subsectionId);
          const toggle = document.getElementById('subtoggle-' + subsectionId);
          
          if (content.classList.contains('collapsed')) {
            content.classList.remove('collapsed');
            toggle.textContent = '▼';
          } else {
            content.classList.add('collapsed');
            toggle.textContent = '▶';
          }
        }
      </script>
    </head>
    <body>
      <div class="container">
        <h1>ESP32 Settings</h1>
        )rawliteral";

        html += "<div class='version'>Version: " + String(APP_VERSION) + "</div>";

        html += "<form method='POST' action='/settings'><div class='form-group'>";

        // Get sorted sections
        auto sortedSections = getSortedSections();

        // Process each section
        for (const auto& section : sortedSections) {
            // Skip subsections, they'll be handled within parent sections
            if (!section.parentId.isEmpty())
                continue;

            String sectionId = section.id;
            String contentId = "content-" + sectionId;
            String toggleId = "toggle-" + sectionId;

            html += "<div class='section'>";

            // Section header
            if (section.collapsible) {
                html += "<div class='section-header' onclick='toggleSection(\"" + sectionId + "\")'>";
            } else {
                html += "<div class='section-header'>";
            }

            html += "<div>";
            html += "<div class='section-title'>" + section.title + "</div>";
            if (!section.description.isEmpty()) {
                html += "<div class='section-description'>" + section.description + "</div>";
            }
            html += "</div>";

            if (section.collapsible) {
                String toggleChar = section.startCollapsed ? "▶" : "▼";
                html += "<span class='section-toggle' id='" + toggleId + "'>" + toggleChar + "</span>";
            }
            html += "</div>";

            // Section content
            String collapsedClass = (section.startCollapsed && section.collapsible) ? " collapsed" : "";
            html += "<div class='section-content" + collapsedClass + "' id='" + contentId + "'>";

            // Add settings for this section
            auto sectionSettings = getSettingsForSection(sectionId);
            if (sectionSettings.empty() && sectionId == "general") {
                // Get unsectioned settings for the general section
                sectionSettings = getSettingsForSection("");
            }

            for (const auto& s : sectionSettings) {
                html += generateSettingHTML(s);
            }

            // Add subsections
            for (const auto& subsection : sortedSections) {
                if (subsection.parentId == sectionId) {
                    String subsectionId = subsection.id;
                    String subContentId = "subcontent-" + subsectionId;
                    String subToggleId = "subtoggle-" + subsectionId;

                    html += "<div class='subsection'>";

                    // Subsection header
                    if (subsection.collapsible) {
                        html += "<div class='subsection-header' onclick='toggleSubsection(\"" + subsectionId + "\")'>";
                    } else {
                        html += "<div class='subsection-header'>";
                    }

                    html += "<div>";
                    html += "<div class='subsection-title'>" + subsection.title + "</div>";
                    if (!subsection.description.isEmpty()) {
                        html += "<div class='section-description'>" + subsection.description + "</div>";
                    }
                    html += "</div>";

                    if (subsection.collapsible) {
                        String subToggleChar = subsection.startCollapsed ? "▶" : "▼";
                        html += "<span class='subsection-toggle' id='" + subToggleId + "'>" + subToggleChar + "</span>";
                    }
                    html += "</div>";  // Close subsection-header

                    // Subsection content
                    String subCollapsedClass =
                        (subsection.startCollapsed && subsection.collapsible) ? " collapsed" : "";
                    html += "<div class='subsection-content" + subCollapsedClass + "' id='" + subContentId + "'>";

                    auto subsectionSettings = getSettingsForSection(subsection.id);
                    for (const auto& s : subsectionSettings) {
                        html += generateSettingHTML(s);
                    }

                    html += "</div>";  // Close subsection-content
                    html += "</div>";  // Close subsection
                }
            }

            html += "</div></div>";  // Close section-content and section
        }

        html += "</div>";  // Close form-group
        html += "<input type='submit' value='Save Settings'>";
        html += "</form></div></body></html>";

        request->send(200, "text/html", html);
    });

    server.on("/settings", HTTP_POST, [this](AsyncWebServerRequest* request) {
        for (auto& s : settings) {
            // Skip read-only settings
            if (s.readonly)
                continue;

            if (s.type == BOOL) {
                *(bool*)s.value = request->hasParam(s.key, true);
            } else if (s.type == INT) {
                if (request->hasParam(s.key, true))
                    *(int*)s.value = request->getParam(s.key, true)->value().toInt();
            } else if (s.type == FLOAT) {
                if (request->hasParam(s.key, true))
                    *(float*)s.value = request->getParam(s.key, true)->value().toFloat();
            } else if (s.type == STRING) {
                if (request->hasParam(s.key, true))
                    *(String*)s.value = request->getParam(s.key, true)->value();
            } else if (s.type == ARRAY_INT) {
                int* arr = (int*)s.value;
                for (size_t i = 0; i < s.size; i++) {
                    String param = s.key + "_" + String(i);
                    if (request->hasParam(param, true))
                        arr[i] = request->getParam(param, true)->value().toInt();
                }
            } else if (s.type == ARRAY_FLOAT) {
                float* arr = (float*)s.value;
                for (size_t i = 0; i < s.size; i++) {
                    String param = s.key + "_" + String(i);
                    if (request->hasParam(param, true))
                        arr[i] = request->getParam(param, true)->value().toFloat();
                }
            }
        }

        save();  // Save updated values

        // Call post-save callback if set
        if (postSaveCallback) {
            postSaveCallback();
        }

        request->redirect("/settings");
    });
}
