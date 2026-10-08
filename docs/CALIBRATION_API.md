# Calibration web API

The tester can be calibrated over Wi-Fi, without a serial cable. This is the
HTTP API the calibration wizard page uses (phase 1: API only; the wizard page
follows). Everything is reachable on the `Tester` access point at
`http://192.168.4.1`.

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
- **Paths** (`CalibrationPaths.h`) — `Cl-Cr` (default; the model the tester
  uses for all thresholds) and `Bl-Br`. Pins come from `Hardware.h`, so all
  hardware revisions share the table.

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
| POST | `/api/cal/run` | run record JSON, max 768 bytes compacted, must have `"id"` | Store a run record in the tester's ring |
| GET | `/api/cal/runs` | | Stored run records, oldest first |
| POST | `/api/cal/end` | | Leave the `Calibrating` state |

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

# for each resistor between top C and bottom C: note v_diff_mv once stable
curl -s $T/api/cal/sample

# put the values in points.json (format above), then:
curl -s -X POST -H "$J" -d @points.json $T/api/cal/fit
curl -s -X POST $T/api/cal/save
curl -s -X POST $T/api/cal/end
```
