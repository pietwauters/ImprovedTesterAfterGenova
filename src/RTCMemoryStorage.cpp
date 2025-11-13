#include "RTCMemoryStorage.h"

#include <cstddef>  // for offsetof
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_log.h"

static const char* TAG = "RTCMemoryStorage";

// Debug output control for RTC storage debugging
// Set to 1 for detailed debug output, 0 for production builds (saves ~2KB flash)
#define RTC_DEBUG 0

#if RTC_DEBUG
#define RTC_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#define RTC_DEBUG_LOGD(...) ESP_LOGD(__VA_ARGS__)
#define RTC_DEBUG_LOGW(...) ESP_LOGW(__VA_ARGS__)
#define RTC_DEBUG_LOGI(...) ESP_LOGI(__VA_ARGS__)
#else
#define RTC_DEBUG_PRINTF(...) ((void)0)
#define RTC_DEBUG_LOGD(...) ((void)0)
#define RTC_DEBUG_LOGW(...) ((void)0)
#define RTC_DEBUG_LOGI(...) ((void)0)
#endif

// Optimized RTC storage with hash-based keys - dramatically more efficient
RTC_DATA_ATTR static RTCMemoryStorageInternal::RTCStorage rtcData;

RTCMemoryStorage::RTCMemoryStorage() {
    RTC_DEBUG_PRINTF("RTCMemoryStorage constructor - size: %zu bytes, max entries: %d\n", sizeof(rtcData),
                     RTCMemoryStorageInternal::MAX_ENTRIES);
    RTC_DEBUG_PRINTF("RTC data at startup - magic: 0x%08X, expected: 0x%08X\n", rtcData.magic,
                     RTCMemoryStorageInternal::RTC_MAGIC_NUMBER);
    initializeRTCMemory();
    RTC_DEBUG_PRINTF("RTC storage initialized\n");
}

void RTCMemoryStorage::initializeRTCMemory() {
    RTC_DEBUG_PRINTF("RTC initializeRTCMemory: magic: 0x%08X, expected: 0x%08X, stored checksum: 0x%08X\n",
                     rtcData.magic, RTCMemoryStorageInternal::RTC_MAGIC_NUMBER, rtcData.checksum);
    RTC_DEBUG_PRINTF("RTC initializeRTCMemory: entryCount: %zu\n", rtcData.entryCount);

    // Check if magic is correct
    if (rtcData.magic != RTCMemoryStorageInternal::RTC_MAGIC_NUMBER) {
        RTC_DEBUG_PRINTF("RTC initializeRTCMemory: Invalid magic - initializing fresh data\n");
        rtcData.magic = RTCMemoryStorageInternal::RTC_MAGIC_NUMBER;
        rtcData.entryCount = 0;

        // Clear all entries
        for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
            rtcData.entries[i].keyHash = 0;
            rtcData.entries[i].dataType = RTCMemoryStorageInternal::RTCDataType::INTEGER;
            rtcData.entries[i].data.intValue = 0;
            rtcData.entries[i].valid = 0;
        }
        rtcData.checksum = calculateSimpleChecksum();
        RTC_DEBUG_PRINTF("RTC initializeRTCMemory: Fresh data initialized - new checksum: 0x%08X\n", rtcData.checksum);
    } else {
        RTC_DEBUG_PRINTF("RTC initializeRTCMemory: Magic valid - checking existing data\n");
        uint32_t calculatedChecksum = calculateSimpleChecksum();
        RTC_DEBUG_PRINTF("RTC initializeRTCMemory: calculated checksum: 0x%08X vs stored: 0x%08X\n", calculatedChecksum,
                         rtcData.checksum);

        if (rtcData.checksum != calculatedChecksum) {
            RTC_DEBUG_PRINTF("RTC initializeRTCMemory: CHECKSUM MISMATCH - forcing re-initialization\n");
            // Force re-initialization if checksum doesn't match
            rtcData.entryCount = 0;
            for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
                rtcData.entries[i].keyHash = 0;
                rtcData.entries[i].dataType = RTCMemoryStorageInternal::RTCDataType::INTEGER;
                rtcData.entries[i].data.intValue = 0;
                rtcData.entries[i].valid = 0;
            }
            rtcData.checksum = calculateSimpleChecksum();
            RTC_DEBUG_PRINTF("RTC initializeRTCMemory: Force re-init complete - new checksum: 0x%08X\n",
                             rtcData.checksum);
        } else {
            RTC_DEBUG_PRINTF("RTC initializeRTCMemory: Data appears valid - entries: %zu\n", rtcData.entryCount);
        }
    }
}
uint32_t RTCMemoryStorage::calculateSimpleChecksum() const {
    uint32_t checksum = rtcData.magic + rtcData.entryCount;

    // Add checksum for each valid entry
    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        if (rtcData.entries[i].valid) {
            // Add key hash
            checksum += rtcData.entries[i].keyHash;

            // Add data based on type
            if (rtcData.entries[i].dataType == RTCMemoryStorageInternal::RTCDataType::INTEGER) {
                checksum += rtcData.entries[i].data.intValue;
            } else {  // float
                union {
                    float f;
                    uint32_t i;
                } converter;
                converter.f = rtcData.entries[i].data.floatValue;
                checksum += converter.i;
            }
            checksum += static_cast<uint32_t>(rtcData.entries[i].dataType);
        }
    }

    return checksum;
}

uint32_t RTCMemoryStorage::hashKey(const char* key) {
    // djb2 hash algorithm - same as SettingsManager
    uint32_t hash = 5381;
    for (int i = 0; key[i] != '\0'; i++) {
        hash = ((hash << 5) + hash) + key[i];  // hash * 33 + c
    }
    return hash;
}

// Helper function to find entry by key hash
int RTCMemoryStorage::findEntryIndex(uint32_t keyHash) const {
    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        if (rtcData.entries[i].valid && rtcData.entries[i].keyHash == keyHash) {
            return i;
        }
    }
    return -1;  // Not found
}

// Helper function to find free entry slot
int RTCMemoryStorage::findFreeSlot() const {
    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        if (!rtcData.entries[i].valid) {
            return i;
        }
    }
    return -1;  // No free slot
}

// Store float value
void RTCMemoryStorage::store(const char* key, float value) {
    uint32_t keyHash = hashKey(key);
    storeByHash(keyHash, value);
    RTC_DEBUG_LOGD(TAG, "Stored float '%s' (hash: 0x%08X) = %.3f", key, keyHash, value);
}

void RTCMemoryStorage::storeByHash(uint32_t keyHash, float value) {
    // Find existing entry or free slot
    int index = findEntryIndex(keyHash);
    if (index == -1) {
        index = findFreeSlot();
        if (index == -1) {
            RTC_DEBUG_LOGW(TAG, "No free slots available for hash 0x%08X", keyHash);
            return;
        }
        // Initialize new entry
        rtcData.entryCount++;
    }

    // Store the float value
    rtcData.entries[index].keyHash = keyHash;
    rtcData.entries[index].data.floatValue = value;
    rtcData.entries[index].dataType = RTCMemoryStorageInternal::RTCDataType::FLOAT;
    rtcData.entries[index].valid = 1;
    rtcData.checksum = calculateSimpleChecksum();

    RTC_DEBUG_LOGD(TAG, "Stored float hash 0x%08X = %.3f", keyHash, value);
}

// Retrieve float value
float RTCMemoryStorage::retrieve(const char* key, float defaultValue) const {
    uint32_t keyHash = hashKey(key);
    float result = retrieveByHash(keyHash, defaultValue);
    if (result != defaultValue) {
        RTC_DEBUG_LOGD(TAG, "Retrieved float '%s' (hash: 0x%08X) = %.3f", key, keyHash, result);
    }
    return result;
}

float RTCMemoryStorage::retrieveByHash(uint32_t keyHash, float defaultValue) const {
    // Verify checksum first
    uint32_t expectedChecksum = calculateSimpleChecksum();
    if (rtcData.checksum != expectedChecksum) {
        RTC_DEBUG_LOGW(TAG, "RTC checksum mismatch - returning default for hash 0x%08X", keyHash);
        return defaultValue;
    }

    // Find the entry
    int index = findEntryIndex(keyHash);
    if (index == -1) {
        RTC_DEBUG_LOGD(TAG, "Hash 0x%08X not found - returning default", keyHash);
        return defaultValue;
    }

    // Check if it's the correct type
    if (rtcData.entries[index].dataType != RTCMemoryStorageInternal::RTCDataType::FLOAT) {
        RTC_DEBUG_LOGW(TAG, "Hash 0x%08X is not a float - returning default", keyHash);
        return defaultValue;
    }

    RTC_DEBUG_LOGD(TAG, "Retrieved float hash 0x%08X = %.3f", keyHash, rtcData.entries[index].data.floatValue);
    return rtcData.entries[index].data.floatValue;
}

// Store integer
void RTCMemoryStorage::store(const char* key, int value) {
    uint32_t keyHash = hashKey(key);
    RTC_DEBUG_PRINTF("RTC store int '%s' -> hash: 0x%08X, value: %d\n", key, keyHash, value);
    storeByHash(keyHash, value);
}

void RTCMemoryStorage::storeByHash(uint32_t keyHash, int value) {
    // Find existing entry or free slot
    int index = findEntryIndex(keyHash);
    RTC_DEBUG_PRINTF("RTC storeByHash: findEntryIndex returned %d for hash 0x%08X\n", index, keyHash);

    if (index == -1) {
        index = findFreeSlot();
        RTC_DEBUG_PRINTF("RTC storeByHash: findFreeSlot returned %d\n", index);
        if (index == -1) {
            RTC_DEBUG_PRINTF("RTC storeByHash: No free slots available for hash 0x%08X\n", keyHash);
            return;
        }
        // Initialize new entry
        rtcData.entryCount++;
        RTC_DEBUG_PRINTF("RTC storeByHash: New entry, entryCount now %zu\n", rtcData.entryCount);
    }

    // Store the int value
    rtcData.entries[index].keyHash = keyHash;
    rtcData.entries[index].data.intValue = value;
    rtcData.entries[index].dataType = RTCMemoryStorageInternal::RTCDataType::INTEGER;
    rtcData.entries[index].valid = 1;
    rtcData.checksum = calculateSimpleChecksum();

    RTC_DEBUG_PRINTF("RTC storeByHash: Stored at index %d - hash: 0x%08X, value: %d, valid: %s, checksum: 0x%08X\n",
                     index, rtcData.entries[index].keyHash, rtcData.entries[index].data.intValue,
                     rtcData.entries[index].valid ? "true" : "false", rtcData.checksum);
}

// Retrieve integer
int RTCMemoryStorage::retrieve(const char* key, int defaultValue) const {
    uint32_t keyHash = hashKey(key);
    RTC_DEBUG_PRINTF("RTC retrieve int '%s' -> hash: 0x%08X\n", key, keyHash);
    int result = retrieveByHash(keyHash, defaultValue);
    RTC_DEBUG_PRINTF("RTC retrieved int '%s' = %d (default was %d)\n", key, result, defaultValue);
    return result;
}

int32_t RTCMemoryStorage::retrieveByHash(uint32_t keyHash, int32_t defaultValue) const {
    // Verify checksum first
    uint32_t expectedChecksum = calculateSimpleChecksum();
    RTC_DEBUG_PRINTF("RTC retrieveByHash: checksum check - stored: 0x%08X, calculated: 0x%08X\n", rtcData.checksum,
                     expectedChecksum);

    if (rtcData.checksum != expectedChecksum) {
        RTC_DEBUG_PRINTF("RTC retrieveByHash: CHECKSUM MISMATCH - returning default for hash 0x%08X\n", keyHash);
        return defaultValue;
    }

    // Find the entry
    int index = findEntryIndex(keyHash);
    RTC_DEBUG_PRINTF("RTC retrieveByHash: findEntryIndex returned %d for hash 0x%08X\n", index, keyHash);

    if (index == -1) {
        RTC_DEBUG_PRINTF("RTC retrieveByHash: Hash 0x%08X not found - returning default %d\n", keyHash, defaultValue);
        return defaultValue;
    }

    // Check if it's the correct type
    if (rtcData.entries[index].dataType != RTCMemoryStorageInternal::RTCDataType::INTEGER) {
        RTC_DEBUG_PRINTF("RTC retrieveByHash: Hash 0x%08X is not an integer - returning default\n", keyHash);
        return defaultValue;
    }

    RTC_DEBUG_PRINTF("RTC retrieveByHash: Found at index %d - hash: 0x%08X, value: %d\n", index,
                     rtcData.entries[index].keyHash, rtcData.entries[index].data.intValue);
    return rtcData.entries[index].data.intValue;
}  // Legacy method name - will be deprecated
float RTCMemoryStorage::retrieveFloat(const char* key, float defaultValue) const { return retrieve(key, defaultValue); }

// Retrieve with string (overload)
float RTCMemoryStorage::retrieve(const std::string& key, float defaultValue) const {
    return retrieve(key.c_str(), defaultValue);
}

int RTCMemoryStorage::retrieve(const std::string& key, int defaultValue) const {
    return retrieve(key.c_str(), defaultValue);
}

// Utility functions
bool RTCMemoryStorage::exists(const char* key) const {
    uint32_t keyHash = hashKey(key);
    return existsByHash(keyHash);
}

bool RTCMemoryStorage::existsByHash(uint32_t keyHash) const { return findEntryIndex(keyHash) != -1; }

bool RTCMemoryStorage::remove(const char* key) {
    uint32_t keyHash = hashKey(key);
    return removeByHash(keyHash);
}

bool RTCMemoryStorage::removeByHash(uint32_t keyHash) {
    int index = findEntryIndex(keyHash);
    if (index == -1) {
        return false;
    }

    rtcData.entries[index].valid = 0;
    rtcData.entries[index].keyHash = 0;
    rtcData.entryCount--;
    rtcData.checksum = calculateSimpleChecksum();

    RTC_DEBUG_LOGD(TAG, "Removed entry with hash 0x%08X", keyHash);
    return true;
}

void RTCMemoryStorage::clear() {
    // Clear all entries
    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        rtcData.entries[i].valid = 0;
        rtcData.entries[i].keyHash = 0;
        rtcData.entries[i].dataType = RTCMemoryStorageInternal::RTCDataType::INTEGER;
        rtcData.entries[i].data.intValue = 0;
    }
    rtcData.entryCount = 0;
    rtcData.checksum = calculateSimpleChecksum();
    RTC_DEBUG_LOGI(TAG, "Cleared RTC data");
}

size_t RTCMemoryStorage::getEntryCount() const { return rtcData.entryCount; }

size_t RTCMemoryStorage::getUsedEntries() const {
    size_t count = 0;
    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        if (rtcData.entries[i].valid) {
            count++;
        }
    }
    return count;
}

size_t RTCMemoryStorage::getMaxEntries() const { return RTCMemoryStorageInternal::MAX_ENTRIES; }

size_t RTCMemoryStorage::getTotalMemoryUsed() const { return sizeof(rtcData); }

uint32_t RTCMemoryStorage::computeHash(const char* key) { return hashKey(key); }

void RTCMemoryStorage::printAll() const {
    RTC_DEBUG_LOGI(TAG, "RTC Memory Storage Contents:");
    RTC_DEBUG_LOGI(TAG, "  Magic: 0x%08X, Checksum: 0x%08X, Entries: %zu/%d", rtcData.magic, rtcData.checksum,
                   rtcData.entryCount, RTCMemoryStorageInternal::MAX_ENTRIES);

    for (size_t i = 0; i < RTCMemoryStorageInternal::MAX_ENTRIES; i++) {
        if (rtcData.entries[i].valid) {
            if (rtcData.entries[i].dataType == RTCMemoryStorageInternal::RTCDataType::INTEGER) {
                RTC_DEBUG_LOGI(TAG, "  [%zu] Hash: 0x%08X = %d (int)", i, rtcData.entries[i].keyHash,
                               rtcData.entries[i].data.intValue);
            } else {
                RTC_DEBUG_LOGI(TAG, "  [%zu] Hash: 0x%08X = %.3f (float)", i, rtcData.entries[i].keyHash,
                               rtcData.entries[i].data.floatValue);
            }
        }
    }
}

bool RTCMemoryStorage::isRTCDataValid() const {
    if (rtcData.magic != RTCMemoryStorageInternal::RTC_MAGIC_NUMBER) {
        return false;
    }

    uint32_t expectedChecksum = calculateSimpleChecksum();
    return rtcData.checksum == expectedChecksum;
}