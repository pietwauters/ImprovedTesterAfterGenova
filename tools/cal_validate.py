#!/usr/bin/env python3
r"""How accurately does the tester's model read the other connections?

The tester has one calibrated model, fitted on socket A top to A bottom. The
weapon and cord tests also use other connections (B-B, C-C, bottom pairs, cross
pairs). This script measures your resistor set on those connections, without
saving anything on the tester, and reports per connection:

  - how the tester's current model reads each resistor (signed error), and
  - how good a model of that connection's own would be (to see whether
    separate models per connection would pay off).

Connect to the "Tester" Wi-Fi, then:

    python3 tools/cal_validate.py                  # A-A reference + priority 1 and 2 connections
    python3 tools/cal_validate.py --priority 3     # all 15 connections
    python3 tools/cal_validate.py --paths Al-Ar,Bl-Br
    python3 tools/cal_validate.py --list

Resistor sets are shared with cal_wizard.py (-r / --set-name). Each resistor is
connected twice to check the contacts. Results are appended to cal_runs.jsonl
(kind "validate"); tools/cal_analyze.py summarises them.
"""

import argparse
import json
import math
import sys
import threading
import time

sys.dont_write_bytecode = True  # no __pycache__ next to the scripts
import cal_wizard as cw  # noqa: E402  (shares the tester API, resistor sets and waiting logic)

# Code path name, priority, what the tests use it for. Physical end points come from the tester.
CONNECTIONS = [
    ("Cl-Cr", 0, "calibrated connection (reference)"),
    ("Al-Ar", 1, "body cord wire"),
    ("Bl-Br", 1, "body cord wire"),
    ("Ar-Cr", 2, "epee loop"),
    ("Ar-Br", 2, "foil loop"),
    ("Br-Cr", 2, "bottom lame"),
    ("Ar-Cl", 2, "tip wire (1 Ohm threshold)"),
    ("Br-Cl", 2, "probe"),
    ("Cr-Al", 3, "mode detection only"),
    ("Cr-Bl", 3, "mode detection only"),
    ("Ar-Bl", 3, "mode detection only"),
    ("Br-Al", 3, "mode detection only"),
    ("Cl-Al", 3, "mode detection only (cable reel)"),
    ("Cl-Bl", 3, "mode detection only"),
    ("Al-Bl", 3, "mode detection only"),
]
USE = {name: use for name, _, use in CONNECTIONS}
# Physical end points, for firmware that does not report them yet (same as src/CalibrationPaths.h)
ENDS = {
    "Cl-Cr": ["A top", "A bottom"], "Al-Ar": ["B top", "B bottom"], "Bl-Br": ["C top", "C bottom"],
    "Ar-Cr": ["B bottom", "A bottom"], "Ar-Br": ["B bottom", "C bottom"], "Br-Cr": ["C bottom", "A bottom"],
    "Ar-Cl": ["B bottom", "A top"], "Br-Cl": ["C bottom", "A top"], "Cr-Al": ["A bottom", "B top"],
    "Cr-Bl": ["A bottom", "C top"], "Ar-Bl": ["B bottom", "C top"], "Br-Al": ["C bottom", "B top"],
    "Cl-Al": ["A top", "B top"], "Cl-Bl": ["A top", "C top"], "Al-Bl": ["B top", "C top"],
}

READINGS_PER_WINDOW = 12 * 32  # tester averaging window: 12 readings of 32 measurements
MAX_CAPTURES = 4


def model_resistance(model_json, v_diff_mv):
    """The firmware's modelResistance(), for a model as /api/cal returns it (M2, or the older Rs + c/R)."""
    g = model_json["v_gpio_mv"] / 1000
    k = model_json["r1_r2_ohm"]
    v = v_diff_mv / 1000
    if "r_internal_ohm" in model_json:
        if v >= g:
            return -1.0
        return max(0.0, k * v / (g - v) - model_json["r_internal_ohm"]) if v > 0 else 0.0
    c = model_json["correction_ohm2"]
    if v <= 0 or v >= g:
        return -1.0
    a, b, cc = v - g, v * k, v * c
    d = b * b - 4 * a * cc
    if d < 0:
        return -1.0
    q = -0.5 * (b + math.copysign(math.sqrt(d), b))
    roots = [x for x in (q / a, cc / q) if x > 0]
    return max(roots) if roots else -1.0


def signed_error(model_json, r, v_diff_mv):
    """Error in % of max(R, 1 Ohm), as the firmware judges it."""
    est = model_resistance(model_json, v_diff_mv)
    return est, ((est - r) / max(r, 1.0) * 100) if est >= 0 else float("nan")


def agreement(a, b):
    v = (a["v_diff_mv"] + b["v_diff_mv"]) / 2
    noise = math.sqrt((a.get("noise_sd_mv", 0) ** 2 + b.get("noise_sd_mv", 0) ** 2) / READINGS_PER_WINDOW)
    return abs(a["v_diff_mv"] - b["v_diff_mv"]) / v * 100, max(0.5, 4 * noise / v * 100)


def mean_sample(a, b):
    return {k: (a[k] + b[k]) / 2 for k in a if isinstance(a.get(k), (int, float)) and isinstance(b.get(k), (int, float))}


def estimate_r(points, v_high, v_diff):
    """Resistance from this connection's own points so far (plain divider), for the wrong-resistor check."""
    if not points or v_diff <= 0 or v_diff >= v_high:
        return None
    rs = sorted(p["r_ohm"] * (v_high - p["v_diff_mv"]) / p["v_diff_mv"] for p in points)
    return rs[len(rs) // 2] * v_diff / (v_high - v_diff)


def capture_point(tester, r, resistors, points, v_high, where):
    """Capture one resistor twice (reseated in between); returns the mean sample and [captures, spread %]."""
    caps, note = [], ""
    while True:
        if caps:
            print(f"  Connect the {r:g} Ohm resistor again (contact check). {note}".rstrip())
        else:
            print(f"  Connect the {r:g} Ohm resistor between {where}.")
        s = cw.wait_for(tester, lambda s: not s.get("open"), f"{r:g} Ohm")
        if not caps:
            est = estimate_r(points, v_high, s["v_diff_mv"])
            if est is not None:
                guess = cw.closest(est, resistors)
                if guess != r and abs(est - r) / r > 0.15:
                    print(f"  This reads like the {guess:g} Ohm resistor. Press Enter to keep it, "
                          f"or type r to measure again.")
                    if input("  > ").strip().lower() == "r":
                        print(f"  Remove the resistor, then connect the {r:g} Ohm resistor again.")
                        cw.wait_until_open(tester)
                        continue
        caps.append(s)
        print(f"  Captured: {s['v_diff_mv']:.2f} mV. Remove the resistor.")
        cw.wait_until_open(tester)
        if len(caps) < 2:
            continue
        best = None
        for i in range(len(caps)):
            for j in range(i + 1, len(caps)):
                spread, tol = agreement(caps[i], caps[j])
                if best is None or spread < best[0]:
                    best = (spread, tol, caps[i], caps[j])
        spread, tol, a, b = best
        if spread <= tol:
            return mean_sample(a, b), [len(caps), round(spread, 2)]
        if len(caps) < MAX_CAPTURES:
            note = f"The readings differ by {spread:.2f} %: clean or reseat the contacts."
            continue
        print(f"  The readings keep differing (closest two: {spread:.2f} %). Clean the clips and sockets.")
        if input("  Enter: use the closest two, a: start this resistor again > ").strip().lower() == "a":
            caps, note = [], ""
            continue
        return mean_sample(a, b), [len(caps), round(spread, 2)]


def measure_connection(tester, info, path, set_name, resistors, prompt):
    ends = info.get("ends", {}).get(path) or ENDS.get(path, ["?", "?"])
    where = f"socket {ends[0]} and socket {ends[1]}"
    print(f"\n=== {path}: {ends[0]} - {ends[1]}  ({USE.get(path, '')}) ===")
    if prompt:
        answer = input("  Enter: measure, s: skip, q: stop > ").strip().lower()
        if answer in ("s", "q"):
            return answer
    tester.post("/api/cal/begin", {"path": path})
    for _ in range(50):
        if tester.get("/api/cal/info").get("active"):
            break
        cw.status("  Waiting for the tester to enter calibration mode (unplug any cord or weapon) ...")
        time.sleep(cw.POLL_S)
    else:
        raise SystemExit("\nThe tester did not enter calibration mode. Unplug everything and retry.")
    print("  Remove everything from the tester.")
    open_s = cw.wait_for(tester, lambda s: s.get("open") and s.get("path") == path, "Open circuit")
    v_high = open_s.get("v_high_mv", open_s["v_top_mv"])

    points, raw, contact = [], [], []
    for r in resistors:
        s, c = capture_point(tester, r, resistors, points, v_high, where)
        points.append({"r_ohm": r, "v_diff_mv": s["v_diff_mv"]})
        raw.append([r, s["v_top_mv"], s["v_bottom_mv"], s["v_diff_mv"], s.get("noise_sd_mv", 0),
                    s.get("v_top_rev_mv", 0), s.get("v_diff_rev_mv", 0)])
        contact.append(c)

    fit = tester.post("/api/cal/fit", {"path": path, "open": {"v_high_mv": v_high}, "points": points})
    active, own = info["active_model"], fit["model"]
    print(f"\n  {'R ref':>7}  {'tester reads':>12}  {'error':>8}   {'own model error':>15}")
    act_err, own_err = [], []
    for p in points:
        est, e = signed_error(active, p["r_ohm"], p["v_diff_mv"])
        _, eo = signed_error(own, p["r_ohm"], p["v_diff_mv"])
        act_err.append(e)
        own_err.append(eo)
        print(f"  {p['r_ohm']:7.3f}  {est:10.3f} Ohm  {e:+7.2f} %   {eo:+13.2f} %")
    worst_act = max(act_err, key=abs)
    rms_act = math.sqrt(sum(e * e for e in act_err) / len(act_err))
    worst_own = max(map(abs, own_err))
    print(f"  Tester's model: worst {worst_act:+.2f} %, RMS {rms_act:.2f} %.  Own model: worst {worst_own:.2f} %.")

    run_id = int(time.time())
    record = {
        "id": run_id, "ts": run_id, "fmt": 2, "kind": "validate", "path": path, "ends": ends,
        "use": USE.get(path, ""), "mac": info["mac"], "name": info["name"], "hw": info["hw_rev"], "fw": info["fw"],
        "set": set_name,
        "open": [open_s["v_top_mv"], open_s["v_bottom_mv"], open_s["v_diff_mv"],
                 open_s.get("v_top_rev_mv", 0), open_s.get("v_diff_rev_mv", 0)],
        # [r_ohm, v_top_mv, v_bottom_mv, v_diff_mv (average), noise_sd_mv, reversed v_top_mv, reversed v_diff_mv]
        "pts": raw, "contact": contact,
        "model": "m2" if "r_internal_ohm" in active else "m0",  # active/own: [v_gpio_mv, Rs, Ri or c]
        "active": [active["v_gpio_mv"], active["r1_r2_ohm"], active.get("r_internal_ohm", active.get("correction_ohm2"))],
        "own": [own["v_gpio_mv"], own["r1_r2_ohm"], own.get("r_internal_ohm", own.get("correction_ohm2"))],
        "result": {"active_worst": round(worst_act, 3), "active_rms": round(rms_act, 3), "own_worst": round(worst_own, 3)},
    }
    return record


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-r", "--resistors", help="resistor values in Ohm (4 to 8); remembered as with cal_wizard.py")
    ap.add_argument("--set-name", help="stored resistor set to use or store")
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--priority", type=int, default=2, help="measure connections up to this priority (1-3, default 2)")
    ap.add_argument("--paths", help="comma-separated code path names instead of --priority, e.g. Al-Ar,Bl-Br")
    ap.add_argument("--no-reference", action="store_true", help="skip the A-A reference connection")
    ap.add_argument("--no-prompt", action="store_true", help="do not ask before each connection")
    ap.add_argument("--runs-file", default="cal_runs.jsonl")
    ap.add_argument("--list", action="store_true", help="list the connections and exit")
    args = ap.parse_args()

    if args.list:
        print("Connections (code name, priority, use); physical end points are shown when measuring:")
        for name, prio, use in CONNECTIONS:
            print(f"  {name:6s}  {prio}  {use}")
        return

    if args.resistors:
        try:
            values = [float(v) for v in args.resistors.split(",")]
        except ValueError:
            raise SystemExit("Resistor values must be numbers, e.g. -r 1,2.2,4.7,10")
        if not 4 <= len(values) <= 8 or min(values) <= 0:
            raise SystemExit("Give 4 to 8 positive resistor values.")
    set_name, resistors = cw.choose_resistors(args)
    resistors = sorted(resistors)
    print(f"Resistors ({set_name}): {', '.join(f'{v:g}' for v in resistors)} Ohm")

    if args.paths:
        paths = [p.strip() for p in args.paths.split(",") if p.strip()]
    else:
        paths = [name for name, prio, _ in CONNECTIONS if 1 <= prio <= args.priority]
        if not args.no_reference:
            paths.insert(0, "Cl-Cr")

    tester = cw.Tester(args.host)
    info = tester.get("/api/cal/info")
    unknown = [p for p in paths if p not in info["paths"]]
    if unknown:
        raise SystemExit(f"The tester does not know {', '.join(unknown)} (firmware too old?). It offers: "
                         + ", ".join(info["paths"]))
    model = next((m for m in info.get("models", []) if m["path"] == info["default_path"]), None)
    origin = "its own calibration" if model and not model.get("outdated") else "the default model of this hardware"
    print(f"Tester '{info['name']}' {info['mac'][-5:]}  hw_rev{info['hw_rev']}  fw {info['fw']}; "
          f"it reads with {origin}.")
    print(f"{len(paths)} connections, each resistor twice: about {len(paths) * len(resistors) * 2} clip changes.")

    stop = threading.Event()
    threading.Thread(target=cw.keep_alive, args=(tester, stop), daemon=True).start()
    results = []
    try:
        for path in paths:
            rec = measure_connection(tester, info, path, set_name, resistors, not args.no_prompt)
            if rec == "q":
                break
            if rec == "s":
                continue
            results.append(rec)
            with open(args.runs_file, "a") as f:
                f.write(json.dumps(rec) + "\n")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        stop.set()
        tester.post("/api/cal/end")

    if results:
        print(f"\nSummary ({len(results)} connections, recorded in {args.runs_file}):")
        print(f"  {'path':6s}  {'connection':22s}  {'tester model worst':>18}  {'RMS':>6}  {'own model worst':>15}  use")
        for rec in results:
            res = rec["result"]
            print(f"  {rec['path']:6s}  {rec['ends'][0] + ' - ' + rec['ends'][1]:22s}  {res['active_worst']:+16.2f} %"
                  f"  {res['active_rms']:4.2f} %  {res['own_worst']:13.2f} %  {rec['use']}")
    print("The tester is back in normal mode.")


if __name__ == "__main__":
    main()
