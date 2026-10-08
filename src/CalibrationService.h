#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "CalibrationPaths.h"
#include "adc_calibrator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Wi-Fi power manager lock held while a calibration session is open
constexpr const char* CalibrationWiFiLock = "calibration";

// What the wizard tells the operator; the tester mirrors it on the LED matrix
enum CalFeedback { CalFeedbackNone, CalFeedbackReset, CalFeedbackCaptured, CalFeedbackPass, CalFeedbackFail };

// Live reading of the calibration path, averaged over the last readings
struct CalSample {
    bool valid;         // at least one reading since the path was selected
    uint32_t seq;       // increments with every reading
    float v_top_mv;     // window means
    float v_bottom_mv;
    float v_diff_mv;
    float range_mv;     // max - min of V_diff over the window
    float noise_sd_mv;  // per-sample V_diff spread of the latest reading
    int window;         // readings in the window
    int samples;        // ADC samples per reading after trimming
    bool stable;        // window full and range within the stability limit
    bool open;          // nothing (or more than OpenOhm) connected
    float r_est;        // resistance with the active model, -1 = none
};

// Shared state between the web API (async_tcp task) and the tester task, which
// owns the measurement hardware. The API only requests; the tester task enters
// and leaves its Calibrating state, measures and applies new models.
class CalibrationService {
   public:
    static constexpr int WindowSize = 12;              // readings; about 1 s at 64 samples per reading
    static constexpr int SamplesPerReading = 64;
    static constexpr float StableAbsMv = 1.5f;         // window range limit, or
    static constexpr float StableRelPercent = 0.3f;    // this share of V_diff, whichever is larger
    static constexpr float OpenOhm = 200.0f;           // above this the path counts as open
    static constexpr uint32_t IdleTimeoutMs = 5UL * 60UL * 1000UL;

    void begin();

    // --- web API side ---
    void touch();  // any API call: postpones the idle timeout
    void requestActive(bool on);
    bool selectPath(const CalibrationPath* path);  // false for nullptr
    const CalibrationPath& path();
    bool isActive();
    bool isRequested();
    CalSample sample();
    EmpiricalModel activeModel();
    // Remember the latest fit so /save stores exactly what was shown
    void setLastFit(const CalibrationPath& path, const EmpiricalModel& m, CalVerdict verdict);
    bool lastFit(const CalibrationPath*& path, EmpiricalModel& m, CalVerdict& verdict);
    void clearLastFit();
    void queueModel(const EmpiricalModel& m);  // tester task applies it
    void setFeedback(CalFeedback f);

    // --- tester task side ---
    void setActive(bool on);
    void setActiveModel(const EmpiricalModel& m);
    // Dropped when the path changed while it was being measured
    void pushReading(const CalibrationPath& path, const EmpiricalResistorCalibrator::EmpiricalReading& r);
    bool takeQueuedModel(EmpiricalModel& m);
    CalFeedback takeFeedback();
    bool idleTimedOut();

   private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool requested_ = false;
    bool active_ = false;
    const CalibrationPath* path_ = &kDefaultCalibrationPath;
    uint32_t lastTouchMs_ = 0;
    uint32_t lastWiFiActivityMs_ = 0;
    EmpiricalModel activeModel_ = EmpiricalResistorCalibrator::factoryModel();

    // ring of readings
    float top_[WindowSize], bottom_[WindowSize], diff_[WindowSize];
    int count_ = 0, next_ = 0;
    uint32_t seq_ = 0;
    float noiseSd_ = 0.0f;
    int samplesUsed_ = 0;

    bool fitValid_ = false;
    const CalibrationPath* fitPath_ = nullptr;
    EmpiricalModel fitModel_;
    CalVerdict fitVerdict_ = CalFail;

    CalFeedback feedback_ = CalFeedbackNone;

    bool queued_ = false;
    EmpiricalModel queuedModel_;

    void lock() { xSemaphoreTake(mutex_, portMAX_DELAY); }
    void unlock() { xSemaphoreGive(mutex_); }
    void resetWindow();
};

extern CalibrationService calibrationService;

// Registers /api/cal/* on the web server; deviceName is read on each /info call
void registerCalibrationApi(AsyncWebServer& server, const String& deviceName);
