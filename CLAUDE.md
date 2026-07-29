# Improved Tester — Fencing Equipment Tester (ESP32)

Open-source device for testing fencing body cords, weapons (épée/foil), lamés,
guards, and cable reels against FIE resistance regulations. Built on ESP32,
PlatformIO/Arduino framework, C++. See `README.md` for the full user-facing
description of modes and pass/fail thresholds — this file is for the code
architecture.

## Physical model

6 terminals, wired to differential ADC measurement pairs:

- Bottom face sockets: **A, B, C** (right side in code: `Ar`, `Br`, `Cr`) — body
  cord plug from the cable reel side.
- Top face sockets: **A, B, C** + a 3mm socket (left side in code: `Al`, `Bl`,
  `Cl`) — body cord plug from the weapon side. `Al`/piste is also used for the
  M5 cap nut reference contact and reel-mode loop.
- `Terminal` enum (`WireMeasurement.h`): `Ar=0, Br=1, Cr=2, Al=3, Bl=4, Cl=5`.

Pin/ADC-channel mapping is in `src/Hardware.h`. The device measures resistance
between arbitrary terminal pairs via analog switching (`IODirection_*` /
`IOValues_*` macros select which two terminals are connected through the
measurement bridge).

## Measurement layer (post-refactor architecture)

Recently refactored (`refactor/measurement-architecture` branch, merged into
`main`) into clean layers:

- **`MeasurementHardware`** — lowest level, talks to the ADC/switching hardware.
- **`MeasurementCapture`** (`MeasurementCapture.h/.cpp`) — orchestrates hardware
  reads into a `MeasurementSet`: `captureAll` (all 15 terminal-pair
  combinations), `captureMatrix3x3` (9 right-vs-left pairs, used for mode
  detection), `captureStraightOnly` (Ar-Al, Br-Bl, Cr-Cl only, high-res, used
  once wire testing has locked in). Also exposes single-pair measurers
  (`measureArBr()`, `measureBrCl()`, etc.) returning millivolts.
- **`MeasurementAnalysis`** (`MeasurementAnalysis.h/.cpp`) — pure logic, no
  hardware access. Static methods like `isWirePluggedIn`, `isWirePluggedInFoil`,
  `isWirePluggedInEpee`, `isBroken`, `isSwappedWith`. Also keeps legacy
  `int[3][3]`-array overloads for back-compat with older code paths.
- **`WireMeasurement.h`** — core data types: `Measurement` (one terminal-pair
  reading in mV) and `MeasurementSet` (up to 15 measurements + per-wire lead
  resistance + JSON/binary serialization for cross-device communication).
- **`adc_calibrator.h/.cpp`** (`EmpiricalResistorCalibrator`) — converts raw mV
  readings to Ohms (`get_resistance_empirical`) and Ohm thresholds to mV
  (`get_mv_threshold`), using an empirically fitted model (`v_gpio`, `r1_r2`,
  `correction`) from interactive calibration against known resistors. Persists
  to NVS.

`TesterConfig.h/.cpp` exist but are currently empty — not part of the active
build.

## State machine (`Tester` class, `src/tester.h` / `tester.cpp`)

Runs in its own FreeRTOS task (`taskLoop`, pinned to core 1). Core states
(`State_t`): `Waiting`, `EpeeTesting`, `FoilTesting`, `LameTesting`,
`WireTesting_1`, `WireTesting_2`, `ReelTesting` (some special modes run as a
blocking sub-loop called directly from `Waiting` rather than as a distinct
`currentState`).

`handleWaitingState()` is the mode dispatcher: each iteration it captures all
measurements and checks fixed mV thresholds (not yet resistance-calibrated —
these are rough "is something plugged in this way" detectors) to decide which
special test to enter:

| Trigger (in `Waiting`) | Enters |
|---|---|
| `Ar-Cr < Ohm_20` | Épée test (`doEpeeTest`) |
| `Ar-Br < Ohm_20` | Foil test (`doFoilTest`) |
| `Br-Cr < Ohm_20` | Lamé test via bottom crocodile clip (`doLameTest`) |
| `Cr-Cl < Ohm_20` AND `Ar-Al`/`Br-Bl` both open | Lamé/Guard test via top cap-nut touch (`doLameTest_Top`) |
| `Al-Bl < Ohm_50` | Cable reel test (`doReelTest`) — only reachable once `ReelMode` is armed |

Cable Reel mode is armed/disarmed by short-circuiting top sockets A+B
(`Ar-Br`... actually gated via `SetWiretestMode(true)` from `doReelTest`, and
toggled off when `Al-Bl` measurement drops below `Ohm_50` while already in
`ReelMode`, per README's "short A+B again to exit").

If no special-mode trigger fires and a body cord is detected across the 3x3
matrix (`MeasurementAnalysis::isWirePluggedIn`), the state machine moves into
`WireTesting_1` → `WireTesting_2`:

- **`WireTesting_1`**: coarse per-wire resistance check + colour feedback,
  counts down `timeToSwitch`; also this is where `AverageLeadResistance` gets
  computed once all 3 straight-through wires read near-zero (cord itself is
  good), which is then used to compensate thresholds in later weapon tests
  (`UpdateThresholdsWithLeadResistance`). Sets blink colour to blue once a good
  cord's lead resistance is captured.
- **`WireTesting_2`**: tight polling loop only watching for a wire going open
  (`>= ReferenceBroken`) to catch intermittent micro-breaks; on a break it
  animates the specific broken wire (`animateSingleWire`).

`SetWiretestMode(bool reelMode)` swaps the active `Reference{Broken,Green,
Yellow,Orange,Short}` thresholds between normal body-cord values and
reel-mode values (reel uses much higher tolerances — reel wires are longer).

Each special test (`doEpeeTest`, `doFoilTest`, `doFoilLeakTest`, `doLameTest`,
`doLameTest_Top`, `doReelTest`) is a blocking `while` loop that keeps
re-measuring and updating the LED matrix / OLED until the operator unplugs
(returns to `Waiting`). `doCommonReturnFromSpecialMode()` handles the shared
teardown (blink restart, LED clear) after `doEpeeTest`/`doFoilTest`/
`doLameTest`/`doLameTest_Top` exit.

## Thresholds

All pass/fail colour breakpoints (green/yellow/orange/red/broken) are derived
at runtime in `Tester::UpdateThresholdsWithLeadResistance(RLead)` from a small
set of user-configurable base values in Ohms (`BodycordThreshold`,
`ReelBodycordThreshold`, `FoilSingleWireThreshold`, `FoilLoopThreshold`,
`FoilMassProbeThreshold`, `EpeeSingleWireThreshold`, `EpeeLoopThreshold`,
`LameThreshold` — all declared in `main.cpp`, registered with `SettingsManager`
so they're editable via the web/serial terminal, see `SETTINGS_README.md`).
Colour bands are typically ×1 (green cutoff), ×2 (yellow cutoff), ×4 (orange
cutoff) of the base threshold, converted from Ohms to mV via
`mycalibrator.get_mv_threshold(ohms, leadResistance)`. Separate from these are
fixed-mV **mode-switching detection thresholds** (`ProbeConnectedThreshold`,
`EpeeTipContactThreshold`, `ShortDetectThreshold`, `WireConnectedThreshold` in
`tester.h`) — these decide what the operator is doing (tip touching probe vs.
non-conductive surface, wires shorted for mode exit, etc.), independent of the
Ohm-based pass/fail colour thresholds.

## Surrounding system (`main.cpp`)

- `SettingsManager` — persisted, user-editable settings (thresholds, WiFi,
  device name), exposed over both a web UI (`WebTerminal`) and USB serial
  (`USBSerialTerminal`).
- `DisplayManager` (`Display`) — abstracts the 5×5 WS2812B LED matrix
  (`WS2812BLedMatrix`) and, on the Pro variant, an OLED (Adafruit SSD1306)
  showing exact resistance values.
- `WiFiPowerManager` / `DeepSleepHandler` / `RTCMemoryStorage` — battery/power
  management; device deep-sleeps after inactivity and restores lead-resistance
  calibration from RTC memory on wake.

## Open questions worth confirming with the user before relying on

- Whether `doLameTest_Top` genuinely serves both the "Lamé via top touch" and
  "Guard" modes described in the README (they share thresholds/shape in code,
  README lists slightly different colour bands for guard vs lamé).
- `ResistanceLimits.cpp` (`Max_GuardResistance_Ohm`, `Max_TipResistance_Ohm`,
  etc.) looks like an older/parallel threshold model — check whether it's
  still wired into the build or superseded by the `main.cpp` globals +
  `UpdateThresholdsWithLeadResistance` path before editing thresholds there.
