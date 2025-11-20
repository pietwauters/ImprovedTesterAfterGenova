#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#include <functional>
#include <vector>

class SettingsManager {
   public:
    enum SettingType { BOOL, INT, FLOAT, STRING, ARRAY_INT, ARRAY_FLOAT };

    struct SettingGroup {
        String id;
        String title;
        String description;
        String parentId;   // empty for top-level sections
        String iconClass;  // for future CSS styling
        int order;
        bool collapsible;
        bool startCollapsed;

        // Constructor for easy creation
        SettingGroup(const String& id, const String& title, int order = 0, bool collapsible = true,
                     bool startCollapsed = false)
            : id(id), title(title), order(order), collapsible(collapsible), startCollapsed(startCollapsed) {}

        SettingGroup() : order(0), collapsible(true), startCollapsed(false) {}
    };

    struct Setting {
        String key;    // key for preferences (base key)
        String label;  // label for webpage
        SettingType type;
        void* value;       // pointer to the variable or array
        size_t size;       // for arrays, number of elements
        String sectionId;  // NEW: which section this belongs to
        String helpText;   // NEW: optional tooltip/help
        bool readonly;     // NEW: for display-only settings

        // Constructor for backward compatibility
        Setting(String key, String label, SettingType type, void* value, size_t size, String sectionId = "")
            : key(key), label(label), type(type), value(value), size(size), sectionId(sectionId), readonly(false) {}

        Setting() : type(BOOL), value(nullptr), size(0), readonly(false) {}
    };

    SettingsManager() = default;

    // Section Management
    void addSection(const char* id, const char* title, int order = 0, bool collapsible = true,
                    bool startCollapsed = false);
    void addSubsection(const char* id, const char* title, const char* parentId, int order = 0, bool collapsible = true,
                       bool startCollapsed = false);
    void setSectionDescription(const char* sectionId, const char* description);
    void setSectionIcon(const char* sectionId, const char* iconClass);

    // Add settings (enhanced with optional section assignment)
    void addBool(const char* key, const char* label, bool* value, const char* sectionId = nullptr);
    void addInt(const char* key, const char* label, int* value, const char* sectionId = nullptr);
    void addFloat(const char* key, const char* label, float* value, const char* sectionId = nullptr);
    void addString(const char* key, const char* label, String* value, const char* sectionId = nullptr);
    void addIntArray(const char* key, const char* label, int* array, size_t size, const char* sectionId = nullptr);
    void addFloatArray(const char* key, const char* label, float* array, size_t size, const char* sectionId = nullptr);

    // Utility methods for settings
    void setSettingHelp(const char* key, const char* helpText);
    void setSettingReadonly(const char* key, bool readonly);
    String getPrefKey(const String& key);

    void begin(const String& ns);  // namespace for Preferences
    void load();
    void save();

    void addWebEndpoints(AsyncWebServer& server);

    // Check if a key exists in NVS
    bool keyExists(const String& key);
    bool keyExists(const char* key);

    // Check if a setting (from registered settings) exists in NVS
    bool settingExists(const String& settingKey);
    bool settingExists(const char* settingKey);

    // Add callback for when settings are saved via web interface
    void setPostSaveCallback(std::function<void()> callback);

   private:
    Preferences prefs;
    String namespaceStr;
    std::function<void()> postSaveCallback = nullptr;

    std::vector<Setting> settings;
    std::vector<SettingGroup> sections;

    // Hash-based safe key generator
    String makeHashedKey(const String& base, size_t index);

    // FNV-1a hash function for strings
    uint32_t fnv1aHash(const String& str);

    String generateHTML();
    String generateSettingHTML(const Setting& s);

    // Helper methods for section management
    SettingGroup* findSection(const String& id);
    std::vector<SettingGroup> getSortedSections() const;
    std::vector<Setting> getSettingsForSection(const String& sectionId) const;
    void ensureDefaultSection();
};
