#include "CalibrationStore.h"

#include "cJSON.h"
#include "nvs.h"

CalibrationStore calibrationStore;

static const char* kNamespace = "cal_store";
static constexpr uint16_t kStoredModelVersion = 1;
// Keep this many NVS entries (32 bytes each) free for the settings and everything else
static constexpr size_t kNvsReserveEntries = 64;

const char* calModelTypeName(uint8_t type) {
    switch (type) {
        case CalModelEmpiricalV1:
            return "empirical-v1";
        case CalModelEmpiricalBidir:
            return "empirical-bidir";
        case CalModelDividerRi:
            return "divider-ri";
        default:
            return "unknown";
    }
}

static void modelKey(char* key, size_t size, char prefix, const CalibrationPath& path) {
    snprintf(key, size, "%c_%s", prefix, path.name);  // NVS keys are at most 15 characters
}

static void runKey(char* key, size_t size, int slot) { snprintf(key, size, "run%d", slot); }

bool CalibrationStore::readModel(const char* key, StoredModel& out) const {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    StoredModel m;
    size_t size = sizeof(m);
    esp_err_t err = nvs_get_blob(handle, key, &m, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof(m) || m.version != kStoredModelVersion ||
        (m.type != CalModelEmpiricalV1 && m.type != CalModelEmpiricalBidir && m.type != CalModelDividerRi)) {
        return false;
    }
    out = m;
    return true;
}

bool CalibrationStore::writeModel(const char* key, const StoredModel& m) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    esp_err_t err = nvs_set_blob(handle, key, &m, sizeof(m));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool CalibrationStore::load(const CalibrationPath& path, StoredModel& out) const {
    char key[16];
    modelKey(key, sizeof(key), 'm', path);
    return readModel(key, out);
}

bool usableModel(const StoredModel& stored, EmpiricalModel& out) {
    switch (stored.type) {
        case CalModelDividerRi:
            out = stored.params;
            return true;
        case CalModelEmpiricalBidir:
            // V = Vg R/(R + Rs + c/R) behaves like an internal series resistance Ri = -c/Rs
            out = {stored.params.v_gpio, stored.params.r1_r2, -stored.params.r_internal / stored.params.r1_r2};
            return stored.params.r1_r2 > 0;
        default:
            return false;
    }
}

bool CalibrationStore::loadActive(const CalibrationPath& path, EmpiricalModel& out, StoredModel* stored) const {
    StoredModel m;
    if ((load(path, m) && usableModel(m, out)) || (load(kDefaultCalibrationPath, m) && usableModel(m, out))) {
        if (stored != nullptr) {
            *stored = m;
        }
        return true;
    }
    return false;
}

bool CalibrationStore::hasPrevious(const CalibrationPath& path) const {
    char key[16];
    modelKey(key, sizeof(key), 'p', path);
    StoredModel m;
    return readModel(key, m);
}

bool CalibrationStore::save(const CalibrationPath& path, const EmpiricalModel& params, uint32_t runId) {
    char current[16], previous[16];
    modelKey(current, sizeof(current), 'm', path);
    modelKey(previous, sizeof(previous), 'p', path);

    StoredModel old;
    if (readModel(current, old) && !writeModel(previous, old)) {
        return false;
    }
    StoredModel m = {kStoredModelVersion, CalModelDividerRi, 0, params, runId};
    if (!writeModel(current, m)) {
        return false;
    }
    printf("Calibration saved for path %s: V_gpio=%.1fmV, Rs=%.2fΩ, Ri=%.3fΩ\n", path.name, params.v_gpio * 1000,
           params.r1_r2, params.r_internal);
    return true;
}

bool CalibrationStore::undo(const CalibrationPath& path, StoredModel& restored) {
    char current[16], previous[16];
    modelKey(current, sizeof(current), 'm', path);
    modelKey(previous, sizeof(previous), 'p', path);

    StoredModel cur, prev;
    if (!readModel(previous, prev) || !readModel(current, cur)) {
        return false;
    }
    // Swapped, so a second undo redoes
    if (!writeModel(current, prev) || !writeModel(previous, cur)) {
        return false;
    }
    restored = prev;
    return true;
}

static void readRing(nvs_handle_t handle, uint8_t& next, uint8_t& count) {
    next = 0;
    count = 0;
    nvs_get_u8(handle, "run_next", &next);
    nvs_get_u8(handle, "run_n", &count);
    if (next >= CalibrationStore::MaxRuns || count > CalibrationStore::MaxRuns) {
        next = 0;
        count = 0;
    }
}

bool CalibrationStore::addRun(const char* json, String& error) {
    cJSON* root = cJSON_Parse(json);
    if (root == nullptr || !cJSON_IsObject(root) || cJSON_GetObjectItemCaseSensitive(root, "id") == nullptr) {
        cJSON_Delete(root);
        error = "run must be a JSON object with an \"id\"";
        return false;
    }
    char* compact = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (compact == nullptr) {
        error = "out of memory";
        return false;
    }
    size_t len = strlen(compact);
    if (len > MaxRunBytes) {
        cJSON_free(compact);
        error = "run record too large";
        return false;
    }

    nvs_stats_t stats;
    size_t needed = len / 32 + 2;
    if (nvs_get_stats(nullptr, &stats) != ESP_OK || stats.free_entries < needed + kNvsReserveEntries) {
        cJSON_free(compact);
        error = "not enough free NVS space";
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        cJSON_free(compact);
        error = "cannot open NVS";
        return false;
    }
    uint8_t next, count;
    readRing(handle, next, count);
    char key[16];
    runKey(key, sizeof(key), next);
    esp_err_t err = nvs_set_str(handle, key, compact);
    cJSON_free(compact);
    if (err == ESP_OK) {
        next = (next + 1) % MaxRuns;
        if (count < MaxRuns) {
            count++;
        }
        err = nvs_set_u8(handle, "run_next", next);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, "run_n", count);
        }
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        error = esp_err_to_name(err);
        return false;
    }
    return true;
}

int CalibrationStore::runCount() const {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    uint8_t next, count;
    readRing(handle, next, count);
    nvs_close(handle);
    return count;
}

bool CalibrationStore::getRun(int i, String& out) const {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    uint8_t next, count;
    readRing(handle, next, count);
    bool ok = false;
    if (i >= 0 && i < count) {
        char key[16];
        runKey(key, sizeof(key), (next - count + i + MaxRuns) % MaxRuns);
        size_t size = MaxRunBytes + 1;
        char* buffer = (char*)malloc(size);  // not on the stack: this runs in the web server task
        if (buffer != nullptr && nvs_get_str(handle, key, buffer, &size) == ESP_OK) {
            out = buffer;
            ok = true;
        }
        free(buffer);
    }
    nvs_close(handle);
    return ok;
}
