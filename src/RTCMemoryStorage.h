#pragma once

#include <cstdint>
#include <string>

#include "esp_system.h"

// Forward declarations and constants
namespace RTCMemoryStorageInternal {
static constexpr size_t MAX_ENTRIES = 16;  // Increased from 4 to 16 - using hashes saves space
static constexpr uint32_t RTC_MAGIC_NUMBER = 0xDEADBEEF;

/**
 * @brief Data types supported by RTC storage
 */
enum class RTCDataType : uint8_t { INTEGER = 0, FLOAT = 1 };

/**
 * @brief Structure for storing key-value pairs in RTC memory
 * Note: Plain C struct without constructors to avoid RTC memory interference
 */
struct RTCEntry {
    uint32_t keyHash;  // 32-bit hash instead of string key - saves 4 bytes per entry
    union {
        int32_t intValue;
        float floatValue;
    } data;
    RTCDataType dataType;
    uint8_t valid;  // Changed from bool to uint8_t for plain C compatibility
};

/**
 * @brief Structure for the complete RTC memory storage
 * Note: Plain C struct without constructors to avoid RTC memory interference
 */
struct RTCStorage {
    uint32_t magic;     // Magic number to verify data integrity
    uint32_t checksum;  // Simple checksum for data validation
    size_t entryCount;  // Number of valid entries
    RTCEntry entries[MAX_ENTRIES];
};
}  // namespace RTCMemoryStorageInternal

/**
 * @brief Class for storing and retrieving integers and floats from RTC memory
 *
 * This class provides a simple interface to store integer and float values in RTC memory
 * that persist through deep sleep cycles. The ESP32 has 8KB of RTC memory
 * available for user data.
 *
 * Usage:
 *   RTCMemoryStorage storage;
 *   storage.store("counter", 42);           // Store integer
 *   storage.store("temperature", 23.5f);    // Store float
 *   int count = storage.retrieve("counter", 0);          // returns 42, or 0 if not found
 *   float temp = storage.retrieveFloat("temperature", 0.0f); // returns 23.5, or 0.0 if not found
 */
class RTCMemoryStorage {
   public:
    /**
     * @brief Maximum number of key-value pairs that can be stored
     */
    static constexpr size_t MAX_ENTRIES = RTCMemoryStorageInternal::MAX_ENTRIES;

   private:
    // Type aliases for cleaner code
    using RTCEntry = RTCMemoryStorageInternal::RTCEntry;
    using RTCStorage = RTCMemoryStorageInternal::RTCStorage;

    /**
     * @brief Magic number to identify valid RTC data
     */
    static constexpr uint32_t RTC_MAGIC_NUMBER = RTCMemoryStorageInternal::RTC_MAGIC_NUMBER;

    /**
     * @brief Get RTC memory storage instance
     */
    static RTCMemoryStorageInternal::RTCStorage* getRTCStorage();

    /**
     * @brief Calculate simple checksum for data validation
     */
    uint32_t calculateSimpleChecksum() const;

    /**
     * @brief Hash a string key using djb2 algorithm (same as SettingsManager)
     */
    static uint32_t hashKey(const char* key);

    /**
     * @brief Find entry index by key hash
     */
    int findEntryIndex(uint32_t keyHash) const;

    /**
     * @brief Find free slot for new entry
     */
    int findFreeSlot() const;

    /**
     * @brief Validate RTC memory data integrity
     */
    bool isDataValid() const;

    /**
     * @brief Initialize RTC memory if needed
     */
    void initializeRTCMemory();

   public:
    /**
     * @brief Constructor - initializes RTC memory storage
     */
    RTCMemoryStorage();

    /**
     * @brief Store an integer value with a string key
     * @param key Unique identifier for the value (arbitrary length)
     * @param value Integer value to store
     * @return true if stored successfully, false if storage is full
     */
    void store(const char* key, int value);

    /**
     * @brief Store an integer value with a pre-computed hash
     * @param keyHash Pre-computed hash of the key
     * @param value Integer value to store
     * @return true if stored successfully, false if storage is full
     */
    void storeByHash(uint32_t keyHash, int value);

    /**
     * @brief Retrieve an integer value by string key
     * @param key Unique identifier for the value
     * @param defaultValue Default value to return if key not found
     * @return Stored value if found, defaultValue otherwise
     */
    int32_t retrieve(const char* key, int32_t defaultValue = 0) const;

    /**
     * @brief Retrieve an integer value by pre-computed hash
     * @param keyHash Pre-computed hash of the key
     * @param defaultValue Default value to return if key not found
     * @return Stored value if found, defaultValue otherwise
     */
    int32_t retrieveByHash(uint32_t keyHash, int32_t defaultValue = 0) const;

    /**
     * @brief Store a float value with a string key
     * @param key Unique identifier for the value (arbitrary length)
     * @param value Float value to store
     * @return true if stored successfully, false if storage is full
     */
    void store(const char* key, float value);

    /**
     * @brief Store a float value with a pre-computed hash
     * @param keyHash Pre-computed hash of the key
     * @param value Float value to store
     * @return true if stored successfully, false if storage is full
     */
    void storeByHash(uint32_t keyHash, float value);

    /**
     * @brief Retrieve a float value by string key (overloaded version)
     * @param key Unique identifier for the value
     * @param defaultValue Default value to return if key not found
     * @return Stored value if found, defaultValue otherwise
     */
    float retrieve(const char* key, float defaultValue) const;

    /**
     * @brief Retrieve a float value by pre-computed hash
     * @param keyHash Pre-computed hash of the key
     * @param defaultValue Default value to return if key not found
     * @return Stored value if found, defaultValue otherwise
     */
    float retrieveByHash(uint32_t keyHash, float defaultValue) const;

    /**
     * @brief Retrieve a float value by key (legacy method name - will be deprecated)
     * @param key Unique identifier for the value
     * @param defaultValue Default value to return if key not found
     * @return Stored value if found, defaultValue otherwise
     */
    float retrieveFloat(const char* key, float defaultValue = 0.0f) const;

    // String overloads for convenience
    float retrieve(const std::string& key, float defaultValue) const;
    int retrieve(const std::string& key, int defaultValue) const;

    /**
     * @brief Check if a key exists in storage
     * @param key Key to check
     * @return true if key exists, false otherwise
     */
    bool exists(const char* key) const;

    /**
     * @brief Check if a key hash exists in storage
     * @param keyHash Hash of the key to check
     * @return true if key exists, false otherwise
     */
    bool existsByHash(uint32_t keyHash) const;

    /**
     * @brief Remove a key-value pair from storage
     * @param key Key to remove
     * @return true if removed successfully, false if key not found
     */
    bool remove(const char* key);

    /**
     * @brief Remove a key-value pair from storage by hash
     * @param keyHash Hash of the key to remove
     * @return true if removed successfully, false if key not found
     */
    bool removeByHash(uint32_t keyHash);

    /**
     * @brief Compute hash for a string key
     * @param key String key to hash
     * @return 32-bit hash value
     */
    static uint32_t computeHash(const char* key);

    /**
     * @brief Clear all stored data
     */
    void clear();

    /**
     * @brief Get number of stored entries
     */
    size_t getEntryCount() const;

    /**
     * @brief Get the maximum number of entries that can be stored
     */
    size_t getMaxEntries() const;

    /**
     * @brief Get the number of used entries
     */
    size_t getUsedEntries() const;

    /**
     * @brief Get memory usage information
     */
    size_t getTotalMemoryUsed() const;

    /**
     * @brief Print all stored key-value pairs for debugging
     */
    void printAll() const;

    /**
     * @brief Check if RTC memory data is valid after wake-up
     * @return true if data is valid, false if corrupted or first boot
     */
    bool isRTCDataValid() const;
};