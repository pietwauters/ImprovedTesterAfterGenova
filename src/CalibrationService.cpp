#include "CalibrationService.h"

#include <math.h>

#include "CalibrationStore.h"
#include "Hardware.h"
#include "WiFiPowerManager.h"
#include "calibrate_html.h"
#include "cJSON.h"
#include "esp_mac.h"
#include "version.h"

CalibrationService calibrationService;

// ============================================================================
// Shared state
// ============================================================================

void CalibrationService::begin() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
}

void CalibrationService::resetWindow() {
    count_ = 0;
    next_ = 0;
}

void CalibrationService::touch() {
    lock();
    uint32_t now = millis();
    lastTouchMs_ = now;
    // Calibration API use counts as Wi-Fi activity, so Wi-Fi never drops right after a
    // session ends; throttled because recordActivity() logs every call
    bool recordWiFi = now - lastWiFiActivityMs_ > 10000;
    if (recordWiFi) {
        lastWiFiActivityMs_ = now;
    }
    unlock();
    if (recordWiFi) {
        wifiPowerManager().recordActivity();
    }
}

void CalibrationService::requestActive(bool on) {
    lock();
    requested_ = on;
    lastTouchMs_ = millis();
    unlock();
}

bool CalibrationService::selectPath(const CalibrationPath* path) {
    if (path == nullptr) {
        return false;
    }
    lock();
    if (path != path_) {
        path_ = path;
        resetWindow();
    }
    unlock();
    return true;
}

const CalibrationPath& CalibrationService::path() {
    lock();
    const CalibrationPath* p = path_;
    unlock();
    return *p;
}

bool CalibrationService::isActive() {
    lock();
    bool a = active_;
    unlock();
    return a;
}

bool CalibrationService::isRequested() {
    lock();
    bool r = requested_;
    unlock();
    return r;
}

CalSample CalibrationService::sample() {
    CalSample s = {};
    lock();
    s.seq = seq_;
    s.window = count_;
    s.samples = samplesUsed_;
    s.noise_sd_mv = noiseSd_;
    s.valid = count_ > 0;
    if (s.valid) {
        float lo = diff_[0], hi = diff_[0];
        for (int i = 0; i < count_; i++) {
            s.v_high_mv += high_[i];
            s.v_top_mv += top_[i];
            s.v_bottom_mv += bottom_[i];
            s.v_diff_mv += diff_[i];
            lo = fminf(lo, diff_[i]);
            hi = fmaxf(hi, diff_[i]);
        }
        s.v_high_mv /= count_;
        s.v_top_mv /= count_;
        s.v_bottom_mv /= count_;
        s.v_diff_mv /= count_;
        s.has_reversed = hasReversed_;
        if (hasReversed_) {
            for (int i = 0; i < count_; i++) {
                s.v_top_rev_mv += topRev_[i];
                s.v_bottom_rev_mv += bottomRev_[i];
                s.v_diff_rev_mv += diffRev_[i];
            }
            s.v_top_rev_mv /= count_;
            s.v_bottom_rev_mv /= count_;
            s.v_diff_rev_mv /= count_;
        }
        s.range_mv = hi - lo;
        float limit = fmaxf(StableAbsMv, fabsf(s.v_diff_mv) * StableRelPercent / 100.0f);
        s.stable = count_ == WindowSize && s.range_mv <= limit;
        s.r_est = EmpiricalResistorCalibrator::modelResistance(activeModel_, s.v_diff_mv / 1000.0f);
        s.open = s.r_est < 0 || s.r_est > OpenOhm;
    } else {
        s.r_est = -1.0f;
    }
    unlock();
    return s;
}

EmpiricalModel CalibrationService::activeModel() {
    lock();
    EmpiricalModel m = activeModel_;
    unlock();
    return m;
}

void CalibrationService::setLastFit(const CalibrationPath& path, const EmpiricalModel& m, CalVerdict verdict) {
    lock();
    fitValid_ = true;
    fitPath_ = &path;
    fitModel_ = m;
    fitVerdict_ = verdict;
    unlock();
}

bool CalibrationService::lastFit(const CalibrationPath*& path, EmpiricalModel& m, CalVerdict& verdict) {
    lock();
    bool valid = fitValid_;
    path = fitPath_;
    m = fitModel_;
    verdict = fitVerdict_;
    unlock();
    return valid;
}

void CalibrationService::clearLastFit() {
    lock();
    fitValid_ = false;
    unlock();
}

void CalibrationService::queueModel(const EmpiricalModel& m) {
    lock();
    queued_ = true;
    queuedModel_ = m;
    unlock();
}

void CalibrationService::setFeedback(CalFeedback f) {
    lock();
    feedback_ = f;
    unlock();
}

CalFeedback CalibrationService::takeFeedback() {
    lock();
    CalFeedback f = feedback_;
    feedback_ = CalFeedbackNone;
    unlock();
    return f;
}

void CalibrationService::setActive(bool on) {
    lock();
    active_ = on;
    if (!on) {
        requested_ = false;
    }
    resetWindow();
    lastTouchMs_ = millis();
    unlock();
}

void CalibrationService::setActiveModel(const EmpiricalModel& m) {
    lock();
    activeModel_ = m;
    unlock();
}

void CalibrationService::pushReading(const CalibrationPath& path,
                                     const EmpiricalResistorCalibrator::EmpiricalReading& r) {
    lock();
    if (&path != path_) {
        unlock();
        return;
    }
    top_[next_] = r.v_top * 1000.0f;
    bottom_[next_] = r.v_bottom * 1000.0f;
    diff_[next_] = r.v_diff * 1000.0f;
    high_[next_] = r.v_high * 1000.0f;
    topRev_[next_] = r.v_top_rev * 1000.0f;
    bottomRev_[next_] = r.v_bottom_rev * 1000.0f;
    diffRev_[next_] = r.v_diff_rev * 1000.0f;
    hasReversed_ = r.has_reversed;
    next_ = (next_ + 1) % WindowSize;
    if (count_ < WindowSize) {
        count_++;
    }
    noiseSd_ = r.v_diff_sd_mv;
    samplesUsed_ = r.samples_used;
    seq_++;
    unlock();
}

bool CalibrationService::takeQueuedModel(EmpiricalModel& m) {
    lock();
    bool q = queued_;
    if (q) {
        m = queuedModel_;
        queued_ = false;
    }
    unlock();
    return q;
}

bool CalibrationService::idleTimedOut() {
    lock();
    bool timedOut = millis() - lastTouchMs_ > IdleTimeoutMs;
    unlock();
    return timedOut;
}

// ============================================================================
// Web API helpers
// ============================================================================

static constexpr size_t kMaxBodyBytes = 2048;

// Body chunks are collected in request->_tempObject, which the request frees
static void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > kMaxBodyBytes) {
        return;
    }
    if (index == 0) {
        request->_tempObject = malloc(total + 1);
    }
    if (request->_tempObject == nullptr) {
        return;
    }
    memcpy((uint8_t*)request->_tempObject + index, data, len);
    if (index + len == total) {
        ((char*)request->_tempObject)[total] = '\0';
    }
}

static void sendJson(AsyncWebServerRequest* request, int code, cJSON* obj) {
    char* text = cJSON_PrintUnformatted(obj);
    request->send(code, "application/json", text != nullptr ? text : "{}");
    cJSON_free(text);
    cJSON_Delete(obj);
}

static void sendError(AsyncWebServerRequest* request, int code, const char* message) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "error", message);
    sendJson(request, code, obj);
}

static void addRounded(cJSON* obj, const char* name, double value, int decimals) {
    double scale = pow(10.0, decimals);
    cJSON_AddNumberToObject(obj, name, round(value * scale) / scale);
}

static cJSON* modelToJson(const EmpiricalModel& m) {
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "type", calModelTypeName(CalModelEmpiricalBidir));
    addRounded(obj, "v_gpio_mv", m.v_gpio * 1000.0, 2);
    addRounded(obj, "r1_r2_ohm", m.r1_r2, 3);
    addRounded(obj, "correction_ohm2", m.correction, 3);
    return obj;
}

// Path from ?path=..., else from the body's "path", else the selected one.
// Sends 400 and returns nullptr for an unknown name.
static const CalibrationPath* pathFromRequest(AsyncWebServerRequest* request, cJSON* body) {
    const char* name = nullptr;
    String param;
    if (request->hasParam("path")) {
        param = request->getParam("path")->value();
        name = param.c_str();
    } else if (body != nullptr) {
        cJSON* p = cJSON_GetObjectItemCaseSensitive(body, "path");
        if (cJSON_IsString(p)) {
            name = p->valuestring;
        }
    }
    if (name == nullptr) {
        return &calibrationService.path();
    }
    const CalibrationPath* path = findCalibrationPath(name);
    if (path == nullptr) {
        sendError(request, 400, "unknown path");
    }
    return path;
}

using JsonRoute = std::function<void(AsyncWebServerRequest*, cJSON* body)>;

// POST route with an optional JSON body; the handler gets nullptr when there is none
static void onPostJson(AsyncWebServer& server, const char* uri, JsonRoute handler) {
    server.on(
        uri, HTTP_POST,
        [handler](AsyncWebServerRequest* request) {
            calibrationService.touch();
            if (request->contentLength() > kMaxBodyBytes) {
                sendError(request, 413, "body too large");
                return;
            }
            if (request->contentLength() > 0 && request->_tempObject == nullptr) {
                // A form-encoded body (curl -d without a JSON content type) never reaches collectBody
                sendError(request, 415, "send the body with Content-Type: application/json");
                return;
            }
            cJSON* body = nullptr;
            if (request->_tempObject != nullptr) {
                body = cJSON_Parse((const char*)request->_tempObject);
                if (body == nullptr) {
                    sendError(request, 400, "invalid JSON");
                    return;
                }
            }
            handler(request, body);
            cJSON_Delete(body);
        },
        nullptr, collectBody);
}

static void addModelInfo(cJSON* models, const CalibrationPath& path) {
    StoredModel m;
    if (!calibrationStore.load(path, m)) {
        return;
    }
    cJSON* obj = modelToJson(m.params);
    cJSON_ReplaceItemInObjectCaseSensitive(obj, "type", cJSON_CreateString(calModelTypeName(m.type)));
    cJSON_AddStringToObject(obj, "path", path.name);
    // Fitted on forward-only readings: no longer used, the tester runs on the default model
    cJSON_AddBoolToObject(obj, "outdated", m.type != CalModelEmpiricalBidir);
    cJSON_AddBoolToObject(obj, "migrated", (m.flags & CalModelMigrated) != 0);
    cJSON_AddNumberToObject(obj, "run_id", m.runId);
    cJSON_AddBoolToObject(obj, "has_previous", calibrationStore.hasPrevious(path));
    cJSON_AddItemToArray(models, obj);
}

static void addEvaluation(cJSON* obj, const char* name, const CalEvaluation& e) {
    cJSON* ev = cJSON_AddObjectToObject(obj, name);
    addRounded(ev, "max_err_pct", e.max_err_pct, 3);
    addRounded(ev, "rms_mv", e.rms_mv, 3);
    cJSON_AddStringToObject(ev, "verdict", calVerdictName(e.verdict));
}

// ============================================================================
// Routes
// ============================================================================

static void handleFit(AsyncWebServerRequest* request, cJSON* body) {
    if (body == nullptr) {
        sendError(request, 400, "body required");
        return;
    }
    const CalibrationPath* path = pathFromRequest(request, body);
    if (path == nullptr) {
        return;
    }
    cJSON* open = cJSON_GetObjectItemCaseSensitive(body, "open");
    // v_high_mv: the open-circuit high side, both directions averaged (v_top_mv: older clients)
    cJSON* openTop = cJSON_GetObjectItemCaseSensitive(open, "v_high_mv");
    if (!cJSON_IsNumber(openTop)) {
        openTop = cJSON_GetObjectItemCaseSensitive(open, "v_top_mv");
    }
    if (!cJSON_IsNumber(openTop)) {
        sendError(request, 400, "open.v_high_mv required");
        return;
    }
    float v_open = (float)openTop->valuedouble / 1000.0f;
    if (v_open < 2.5f || v_open > 3.6f) {
        sendError(request, 422, "open-circuit voltage outside 2.5-3.6 V");
        return;
    }

    cJSON* points = cJSON_GetObjectItemCaseSensitive(body, "points");
    int n = cJSON_GetArraySize(points);
    if (!cJSON_IsArray(points) || n < CalMinPoints || n > CalMaxPoints) {
        sendError(request, 400, "points: 4 to 8 required");
        return;
    }
    float r_ref[CalMaxPoints], v_diff[CalMaxPoints];
    int i = 0;
    cJSON* point;
    cJSON_ArrayForEach(point, points) {
        cJSON* r = cJSON_GetObjectItemCaseSensitive(point, "r_ohm");
        cJSON* v = cJSON_GetObjectItemCaseSensitive(point, "v_diff_mv");
        if (!cJSON_IsNumber(r) || !cJSON_IsNumber(v) || r->valuedouble <= 0 || v->valuedouble <= 0 ||
            v->valuedouble / 1000.0 >= v_open) {
            sendError(request, 400, "each point needs r_ohm > 0 and 0 < v_diff_mv < open.v_high_mv");
            return;
        }
        r_ref[i] = (float)r->valuedouble;
        v_diff[i] = (float)v->valuedouble / 1000.0f;
        i++;
    }

    EmpiricalModel fitted = EmpiricalResistorCalibrator::fit(r_ref, v_diff, n, v_open);
    EmpiricalModel factory = EmpiricalResistorCalibrator::factoryModel();
    EmpiricalModel active = calibrationService.activeModel();
    CalEvaluation eFit = EmpiricalResistorCalibrator::evaluate(fitted, r_ref, v_diff, n);
    CalEvaluation eFactory = EmpiricalResistorCalibrator::evaluate(factory, r_ref, v_diff, n);
    CalEvaluation eActive = EmpiricalResistorCalibrator::evaluate(active, r_ref, v_diff, n);
    calibrationService.setLastFit(*path, fitted, eFit.verdict);

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "path", path->name);
    cJSON_AddStringToObject(obj, "verdict", calVerdictName(eFit.verdict));
    cJSON_AddItemToObject(obj, "model", modelToJson(fitted));
    cJSON_AddItemToObject(obj, "active_model", modelToJson(active));
    addEvaluation(obj, "fit", eFit);
    addEvaluation(obj, "factory", eFactory);
    addEvaluation(obj, "active", eActive);
    cJSON* out = cJSON_AddArrayToObject(obj, "points");
    for (i = 0; i < n; i++) {
        cJSON* p = cJSON_CreateObject();
        addRounded(p, "r_ohm", r_ref[i], 4);
        addRounded(p, "r_est_ohm", eFit.r_est[i], 4);
        addRounded(p, "err_pct", eFit.err_pct[i], 3);
        addRounded(p, "err_pct_factory", eFactory.err_pct[i], 3);
        addRounded(p, "err_pct_active", eActive.err_pct[i], 3);
        cJSON_AddItemToArray(out, p);
    }
    addRounded(obj, "limit_pct", CalPassPercent, 1);
    addRounded(obj, "target_pct", CalExcellentPercent, 1);
    sendJson(request, 200, obj);
}

static void handleSave(AsyncWebServerRequest* request, cJSON* body) {
    const CalibrationPath* path;
    EmpiricalModel m;
    CalVerdict verdict;
    if (!calibrationService.lastFit(path, m, verdict)) {
        sendError(request, 409, "no fit to save; call /api/cal/fit first");
        return;
    }
    if (verdict == CalFail) {
        sendError(request, 409, "fit failed the 5% limit; not saved");
        return;
    }
    uint32_t runId = 0;
    cJSON* id = cJSON_GetObjectItemCaseSensitive(body, "run_id");
    if (cJSON_IsNumber(id) && id->valuedouble > 0) {
        runId = (uint32_t)id->valuedouble;
    }
    if (!calibrationStore.save(*path, m, runId)) {
        sendError(request, 500, "NVS write failed");
        return;
    }
    calibrationService.clearLastFit();
    if (path == &kDefaultCalibrationPath) {
        calibrationService.queueModel(m);  // the tester uses the default path's model
    }
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "saved", true);
    cJSON_AddStringToObject(obj, "path", path->name);
    cJSON_AddItemToObject(obj, "model", modelToJson(m));
    sendJson(request, 200, obj);
}

static void handleUndo(AsyncWebServerRequest* request, cJSON* body) {
    const CalibrationPath* path = pathFromRequest(request, body);
    if (path == nullptr) {
        return;
    }
    StoredModel restored;
    if (!calibrationStore.undo(*path, restored)) {
        sendError(request, 409, "no previous model for this path");
        return;
    }
    if (path == &kDefaultCalibrationPath) {
        // An outdated (forward-only) model is not used: the tester falls back to the default
        calibrationService.queueModel(restored.type == CalModelEmpiricalBidir
                                          ? restored.params
                                          : EmpiricalResistorCalibrator::factoryModel());
    }
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "path", path->name);
    cJSON_AddItemToObject(obj, "model", modelToJson(restored.params));
    cJSON_AddBoolToObject(obj, "outdated", restored.type != CalModelEmpiricalBidir);
    sendJson(request, 200, obj);
}

static void handleSample(AsyncWebServerRequest* request) {
    calibrationService.touch();
    const CalibrationPath* path = pathFromRequest(request, nullptr);
    if (path == nullptr) {
        return;
    }
    calibrationService.selectPath(path);
    cJSON* obj = cJSON_CreateObject();
    bool active = calibrationService.isActive();
    cJSON_AddBoolToObject(obj, "active", active);
    cJSON_AddStringToObject(obj, "path", calibrationService.path().name);
    CalSample s = calibrationService.sample();
    if (active && s.valid) {
        cJSON_AddNumberToObject(obj, "seq", s.seq);
        addRounded(obj, "v_high_mv", s.v_high_mv, 2);
        addRounded(obj, "v_top_mv", s.v_top_mv, 2);
        addRounded(obj, "v_bottom_mv", s.v_bottom_mv, 2);
        addRounded(obj, "v_diff_mv", s.v_diff_mv, 2);
        addRounded(obj, "range_mv", s.range_mv, 2);
        addRounded(obj, "noise_sd_mv", s.noise_sd_mv, 2);
        cJSON_AddNumberToObject(obj, "window", s.window);
        cJSON_AddNumberToObject(obj, "samples", s.samples);
        cJSON_AddBoolToObject(obj, "stable", s.stable);
        cJSON_AddBoolToObject(obj, "open", s.open);
        addRounded(obj, "r_est_ohm", s.r_est, 4);
        if (s.has_reversed) {
            addRounded(obj, "v_top_rev_mv", s.v_top_rev_mv, 2);
            addRounded(obj, "v_bottom_rev_mv", s.v_bottom_rev_mv, 2);
            addRounded(obj, "v_diff_rev_mv", s.v_diff_rev_mv, 2);
        }
    }
    sendJson(request, 200, obj);
}

static void handleInfo(AsyncWebServerRequest* request, const String& deviceName) {
    calibrationService.touch();
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char macText[18];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "mac", macText);
    cJSON_AddStringToObject(obj, "name", deviceName.c_str());
    cJSON_AddStringToObject(obj, "hw_rev", HARDWARE_REV_STR);
    cJSON_AddStringToObject(obj, "fw", APP_VERSION);
    cJSON_AddBoolToObject(obj, "active", calibrationService.isActive());
    cJSON_AddBoolToObject(obj, "requested", calibrationService.isRequested());
    cJSON_AddStringToObject(obj, "default_path", kDefaultCalibrationPath.name);
    cJSON_AddStringToObject(obj, "path", calibrationService.path().name);
    cJSON* paths = cJSON_AddArrayToObject(obj, "paths");
    cJSON* sockets = cJSON_AddObjectToObject(obj, "sockets");  // straight paths: socket letter, top and bottom
    cJSON* ends = cJSON_AddObjectToObject(obj, "ends");        // every path: its two physical end points
    cJSON* models = cJSON_AddArrayToObject(obj, "models");
    for (int i = 0; i < kNumCalibrationPaths; i++) {
        const CalibrationPath& p = kCalibrationPaths[i];
        cJSON_AddItemToArray(paths, cJSON_CreateString(p.name));
        if (p.from[0] == p.to[0]) {
            const char letter[2] = {p.from[0], '\0'};
            cJSON_AddStringToObject(sockets, p.name, letter);
        }
        cJSON* pair = cJSON_AddArrayToObject(ends, p.name);
        cJSON_AddItemToArray(pair, cJSON_CreateString(p.from));
        cJSON_AddItemToArray(pair, cJSON_CreateString(p.to));
        addModelInfo(models, p);
    }
    cJSON_AddItemToObject(obj, "active_model", modelToJson(calibrationService.activeModel()));
    cJSON_AddItemToObject(obj, "factory_model", modelToJson(EmpiricalResistorCalibrator::factoryModel()));
    cJSON_AddNumberToObject(obj, "runs", calibrationStore.runCount());
    cJSON_AddNumberToObject(obj, "max_runs", CalibrationStore::MaxRuns);
    addRounded(obj, "limit_pct", CalPassPercent, 1);
    addRounded(obj, "target_pct", CalExcellentPercent, 1);
    sendJson(request, 200, obj);
}

void registerCalibrationApi(AsyncWebServer& server, const String& deviceName) {
    calibrationService.begin();

    server.on("/api/cal/info", HTTP_GET,
              [&deviceName](AsyncWebServerRequest* request) { handleInfo(request, deviceName); });

    server.on("/api/cal/sample", HTTP_GET, handleSample);

    // The calibration wizard page (web/calibrate.html, gzipped at build time)
    server.on("/calibrate", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncWebServerResponse* response =
            request->beginResponse(200, "text/html", calibrate_html_gz, calibrate_html_gz_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    });

    server.on("/cal", HTTP_GET, [](AsyncWebServerRequest* request) { request->redirect("/calibrate"); });

    onPostJson(server, "/api/cal/feedback", [](AsyncWebServerRequest* request, cJSON* body) {
        cJSON* event = cJSON_GetObjectItemCaseSensitive(body, "event");
        const char* name = cJSON_IsString(event) ? event->valuestring : "";
        CalFeedback f = strcmp(name, "reset") == 0      ? CalFeedbackReset
                        : strcmp(name, "captured") == 0 ? CalFeedbackCaptured
                        : strcmp(name, "pass") == 0     ? CalFeedbackPass
                        : strcmp(name, "fail") == 0     ? CalFeedbackFail
                                                        : CalFeedbackNone;
        if (f == CalFeedbackNone) {
            sendError(request, 400, "event: reset, captured, pass or fail");
            return;
        }
        calibrationService.setFeedback(f);
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(obj, "ok", true);
        sendJson(request, 200, obj);
    });

    onPostJson(server, "/api/cal/begin", [](AsyncWebServerRequest* request, cJSON* body) {
        const CalibrationPath* path = pathFromRequest(request, body);
        if (path == nullptr) {
            return;
        }
        calibrationService.selectPath(path);
        calibrationService.requestActive(true);
        wifiPowerManager().keepWiFiOn(CalibrationWiFiLock);
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(obj, "requested", true);
        cJSON_AddBoolToObject(obj, "active", calibrationService.isActive());
        cJSON_AddStringToObject(obj, "path", path->name);
        sendJson(request, 202, obj);
    });

    onPostJson(server, "/api/cal/end", [](AsyncWebServerRequest* request, cJSON* body) {
        calibrationService.requestActive(false);
        wifiPowerManager().releaseWiFiLock(CalibrationWiFiLock);
        wifiPowerManager().recordActivity();  // the normal Wi-Fi timeout starts from here
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddBoolToObject(obj, "requested", false);
        sendJson(request, 202, obj);
    });

    onPostJson(server, "/api/cal/fit", handleFit);
    onPostJson(server, "/api/cal/save", handleSave);
    onPostJson(server, "/api/cal/undo", handleUndo);

    server.on(
        "/api/cal/run", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            calibrationService.touch();
            if (request->contentLength() > kMaxBodyBytes || request->_tempObject == nullptr) {
                sendError(request, 400, "JSON run record required, with Content-Type: application/json");
                return;
            }
            String error;
            if (!calibrationStore.addRun((const char*)request->_tempObject, error)) {
                sendError(request, 400, error.c_str());
                return;
            }
            cJSON* obj = cJSON_CreateObject();
            cJSON_AddBoolToObject(obj, "stored", true);
            cJSON_AddNumberToObject(obj, "runs", calibrationStore.runCount());
            sendJson(request, 200, obj);
        },
        nullptr, collectBody);

    server.on("/api/cal/runs", HTTP_GET, [](AsyncWebServerRequest* request) {
        calibrationService.touch();
        String text = "[";
        int n = calibrationStore.runCount();
        for (int i = 0; i < n; i++) {
            String run;
            if (calibrationStore.getRun(i, run)) {
                if (text.length() > 1) {
                    text += ",";
                }
                text += run;
            }
        }
        text += "]";
        request->send(200, "application/json", text.c_str());
    });
}
