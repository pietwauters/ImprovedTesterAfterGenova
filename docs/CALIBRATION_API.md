# Calibration over Wi-Fi

The tester can be calibrated over Wi-Fi, without a serial cable. Connect a
phone, tablet or laptop to the `Tester` access point and open
**`http://192.168.4.1/calibrate`** (or `/cal`). The page guides the operator
step by step; resistor sets and the calibration history are kept in that
browser, not on the tester.

The page source is `web/calibrate.html`. `extra_script.py` gzips it into
`src/calibrate_html.h` on every build (only rewritten when the page changed),
so edit the HTML, never the header.

Below is the HTTP API the page uses; `tools/cal_wizard.py` and curl use the
same API.

## How it fits together

- **`EmpiricalResistorCalibrator`** (`adc_calibrator.*`) — model maths
  (`fit`, `evaluate`, `modelResistance`) and `measure(path)`. The serial
  calibration at boot uses the same functions.
- **`CalibrationStore`** (`CalibrationStore.*`) — NVS namespace `cal_store`:
  one model per measurement path (`m_<path>`), the previous one for undo
  (`p_<path>`), and a ring of the last 6 run records (`run0`..`run5`).
  On first boot the legacy `emp_cal` calibration is copied to the default
  path and flagged `migrated`; `emp_cal` itself is left alone.
- **`CalibrationService`** (`CalibrationService.*`) — the web handlers only
  *request*; the tester task enters its `Calibrating` state, measures the
  selected path continuously and applies saved models. Only the tester task
  touches the measurement hardware.
- **Paths** (`CalibrationPaths.h`) — all 15 connections between the six
  terminals, each with its drive configuration, ADC channels and physical
  end points. `Cl-Cr` (socket A top to bottom) is the default: the model the
  tester uses for all thresholds. Pins come from `Hardware.h`, so all hardware
  revisions share the table. Path names use the code's terminal names, which
  differ from the socket letters on the tester (code C = socket A, code A =
  socket B, code B = socket C). `/api/cal/info` returns the physical end points
  as `ends` (and `sockets` for the straight paths).

While a session is open, mode detection is paused, the LED matrix shows a blue
**C**, Wi-Fi stays on and the tester does not sleep. A session ends with
`/api/cal/end` or after 5 minutes without API calls.

Verdict: every point within **2 %** is `excellent`, within **5 %** `pass`,
otherwise `fail`. A failed fit cannot be saved.

## Endpoints

All responses are JSON. Units are in the names (`_mv`, `_ohm`, `_pct`).

| Method | Endpoint | Body | Does |
| --- | --- | --- | --- |
| GET | `/api/cal/info` | | MAC, name, hw_rev, fw, stored models per path, active and factory model, run count |
| POST | `/api/cal/begin` | `{"path":"Cl-Cr"}` (optional) | Request the `Calibrating` state; `active` turns true once the tester task has entered it |
| GET | `/api/cal/sample[?path=]` | | Averaged live reading of the path: `v_top_mv`, `v_bottom_mv`, `v_diff_mv`, `range_mv`, `noise_sd_mv`, `stable`, `open`, `r_est_ohm` (with the active model) |
| POST | `/api/cal/fit` | see below | Fit a model; returns it with per-point errors for the new, factory and active model. Nothing is saved |
| POST | `/api/cal/save` | `{"run_id":123}` (optional) | Save the last fit (refused if there is none or it failed) |
| POST | `/api/cal/undo` | `{"path":"Cl-Cr"}` (optional) | Swap the current and previous model of a path |
| POST | `/api/cal/run` | run record JSON, max 1024 bytes compacted, must have `"id"` | Store a run record in the tester's ring |
| GET | `/api/cal/runs` | | Stored run records, oldest first |
| POST | `/api/cal/feedback` | `{"event":"reset"\|"captured"\|"pass"\|"fail"}` | LED matrix: blue C, green flash on a capture, green or red C for the verdict |
| POST | `/api/cal/end` | | Leave the `Calibrating` state |

Every measurement of the tester (`MeasurementHardware::getDifferentialSample`)
takes half of its samples forward and half with the current reversed, and
returns the average. That cancels offset and gain differences between the two
ADC channels and about half of the ADC's nonlinearity (RMS calibration error
0.9 % -> 0.5 % on two hw_rev3 testers). Calibration averages many of exactly
these readings, so the model matches what the tests measure.

In `/api/cal/sample`, `v_diff_mv` is that averaged reading and `v_high_mv` the
averaged high side (the open-circuit reference sent to `/fit` as
`open.v_high_mv`). `v_top_mv`/`v_bottom_mv` are the forward half,
`v_top_rev_mv`/`v_bottom_rev_mv`/`v_diff_rev_mv` the reversed half on the same
channels (so the bottom channel is the high one). Run records (format 2) keep
both halves; `tools/cal_analyze.py` compares forward, reversed and averaged fits.

Models carry their type: `empirical-bidir` (current) or `empirical-v1` (fitted
on forward-only readings before this change). `/api/cal/info` marks v1 models
`outdated`; the tester does not use them and runs on the default model of its
hardware revision until it is recalibrated. For hw_rev3 that default is a
pooled fit of two calibrated testers (within 1.7 % on both).

`stable` means the last 12 readings (about 1 s) of V_diff lie within
1.5 mV or 0.3 %, whichever is larger. `open` means the path reads more than
200 Ω (or nothing) with the active model.

Fit request:

```json
{
  "path": "Cl-Cr",
  "open": { "v_top_mv": 3129.4 },
  "points": [
    { "r_ohm": 1.0, "v_diff_mv": 26.7 },
    { "r_ohm": 2.2, "v_diff_mv": 58.1 },
    { "r_ohm": 4.7, "v_diff_mv": 121.6 },
    { "r_ohm": 10.0, "v_diff_mv": 248.3 }
  ]
}
```

4 to 8 points; `open.v_top_mv` is the open-circuit `v_top_mv` and must be
between 2500 and 3600 mV.

## Guided calibration script

`tools/cal_wizard.py` (Python 3, standard library only) walks through the
steps, captures each reading once it is stable, fits, asks before saving and
records the run in `cal_runs.jsonl` and on the tester:

```sh
python3 tools/cal_wizard.py -r 1,2.2,4.7,10  # your resistors, remembered on this computer
python3 tools/cal_wizard.py                  # next time: the last set again
python3 tools/cal_wizard.py --list-sets      # named sets: --set-name NAME
```

## Accuracy on other connections

`tools/cal_validate.py` measures the resistor set on other connections (any
path, see `--list`) without saving, and reports per connection how the
tester's model reads each resistor (signed error) and how good a model of the
connection's own would be. Default: A-A as reference, then B-B, C-C, the
bottom pairs (epee loop, foil loop, bottom lame) and the cross pairs used by
the weapon tests (tip wire, probe); `--priority 3` adds the rest. Results go to
`cal_runs.jsonl` (kind `validate`); `tools/cal_analyze.py` lists them.

## Calibrating with curl

Run these one at a time in your shell. POST bodies need the JSON content
type (`-H 'Content-Type: application/json'`); plain `curl -d` sends form data,
which the tester refuses with 415.

```sh
T=http://192.168.4.1
J='Content-Type: application/json'

curl -s -X POST -H "$J" -d '{"path":"Cl-Cr"}' $T/api/cal/begin
curl -s $T/api/cal/info                 # repeat until "active":true

# nothing connected: note v_top_mv as the open-circuit reference
curl -s $T/api/cal/sample               # repeat until "stable":true and "open":true

# for each resistor between socket A on top and socket A on the bottom: note v_diff_mv once stable
curl -s $T/api/cal/sample

# put the values in points.json (format above), then:
curl -s -X POST -H "$J" -d @points.json $T/api/cal/fit
curl -s -X POST $T/api/cal/save
curl -s -X POST $T/api/cal/end
```
