#!/usr/bin/env python3
r"""Guided tester calibration over Wi-Fi, using the /api/cal/* web API.

Connect to the "Tester" Wi-Fi, then run:

    python3 tools/cal_wizard.py -r 1,2.2,4.7,10       # your resistors; remembered as set "default"
    python3 tools/cal_wizard.py                       # next time: uses the last set again
    python3 tools/cal_wizard.py -r 0.998,2.2,4.7,10 --set-name lab   # measured values, own name
    python3 tools/cal_wizard.py --set-name lab        # use a stored set
    python3 tools/cal_wizard.py --list-sets

Resistor sets are stored on this computer, not on the tester (in
~/.config/tester-cal/sets.json, or %APPDATA%\tester-cal\sets.json on Windows).
Without any stored set, 1,2,3,5,8,10,12 Ohm is used.

Readings are captured automatically once they are stable. Every run is appended
to cal_runs.jsonl (for statistics across testers) and stored on the tester.
Only the Python standard library is used.
"""

import argparse
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request

DEFAULT_RESISTORS = [1, 2, 3, 5, 8, 10, 12]
POLL_S = 0.3
WINDOW = 12  # readings in the tester's averaging window (CalibrationService::WindowSize)


class Tester:
    def __init__(self, host):
        self.base = "http://" + host

    def _call(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        headers = {"Content-Type": "application/json"} if data is not None else {}
        for attempt in range(3):
            req = urllib.request.Request(self.base + path, data=data, headers=headers, method=method)
            try:
                with urllib.request.urlopen(req, timeout=5) as resp:
                    return json.loads(resp.read() or b"{}")
            except urllib.error.HTTPError as e:
                text = e.read().decode(errors="replace")
                try:
                    message = json.loads(text).get("error", text)
                except ValueError:
                    message = text
                raise SystemExit(f"\nTester refused {path}: HTTP {e.code}: {message}")
            except (urllib.error.URLError, TimeoutError, ConnectionError) as e:
                if attempt == 2:
                    raise SystemExit(
                        f"\nCannot reach the tester at {self.base} ({e}).\n"
                        "Is this computer on the 'Tester' Wi-Fi? Wi-Fi switches off 90 s after boot "
                        "without activity: power-cycle the tester and start again."
                    )
                time.sleep(1)

    def get(self, path):
        return self._call("GET", path)

    def post(self, path, body=None):
        return self._call("POST", path, body)


def keep_alive(tester, stop):
    """Ping the tester while the script waits at a prompt, so the session does not time out."""
    while not stop.wait(30):
        try:
            tester.get("/api/cal/info")
        except SystemExit:
            pass  # the main loop reports connection problems


def status(text):
    sys.stdout.write("\r\033[K" + text)
    sys.stdout.flush()


def wait_for(tester, want, describe):
    """Poll /sample until want(sample) holds on a stable window measured after the call started."""
    start_seq = None
    while True:
        s = tester.get("/api/cal/sample")
        if not s.get("active"):
            raise SystemExit("\nThe tester left calibration mode (idle timeout or /end).")
        seq = s.get("seq", 0)
        if start_seq is None:
            start_seq = seq
        r = s.get("r_est_ohm", -1)
        reading = "open" if s.get("open") else f"{r:.3f} Ohm"
        status(f"  {describe}: {reading}, V_diff {s.get('v_diff_mv', 0):.1f} mV, "
               f"spread {s.get('range_mv', 0):.2f} mV {'(stable)' if s.get('stable') else ''}")
        if want(s) and s.get("stable") and seq - start_seq >= WINDOW:
            print()
            return s
        time.sleep(POLL_S)


def wait_until_open(tester):
    while True:
        s = tester.get("/api/cal/sample")
        if s.get("open"):
            status("")
            return
        status(f"  Remove the resistor ... ({s.get('r_est_ohm', -1):.3f} Ohm)")
        time.sleep(POLL_S)


def sets_file():
    if os.name == "nt" and os.environ.get("APPDATA"):
        base = os.environ["APPDATA"]
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return os.path.join(base, "tester-cal", "sets.json")


def load_sets():
    try:
        with open(sets_file()) as f:
            data = json.load(f)
        return {"last": data.get("last"), "sets": dict(data.get("sets", {}))}
    except (OSError, ValueError):
        return {"last": None, "sets": {}}


def store_sets(data):
    path = sets_file()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)


def choose_resistors(args):
    """Returns (set name, values); -r values are remembered under the set name."""
    data = load_sets()
    if args.resistors:
        values = sorted(float(v) for v in args.resistors.split(","))
        name = args.set_name or data["last"] or "default"
        data["sets"][name] = values
        data["last"] = name
        store_sets(data)
        print(f"Resistor set '{name}' saved in {sets_file()}")
        return name, values
    if args.set_name:
        if args.set_name not in data["sets"]:
            raise SystemExit(f"No resistor set '{args.set_name}'. Create it with -r ... --set-name {args.set_name}")
        name = args.set_name
    elif data["last"] in data["sets"]:
        name = data["last"]
    else:
        return "builtin", list(DEFAULT_RESISTORS)
    data["last"] = name
    store_sets(data)
    return name, data["sets"][name]


def closest(value, choices):
    return min(choices, key=lambda c: abs(c - value))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-r", "--resistors",
                    help="comma-separated resistor values in Ohm (4 to 8); remembered for next time")
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--path", default="Cl-Cr", help="calibration path (default Cl-Cr)")
    ap.add_argument("--set-name", help="name of the resistor set to use or store (default: the last one used)")
    ap.add_argument("--list-sets", action="store_true", help="show the stored resistor sets and exit")
    ap.add_argument("--runs-file", default="cal_runs.jsonl", help="local file the run records are appended to")
    ap.add_argument("--yes", action="store_true", help="save without asking when the fit passes")
    args = ap.parse_args()

    if args.list_sets:
        data = load_sets()
        print(f"Stored in {sets_file()}:" if data["sets"] else f"No sets stored yet ({sets_file()}).")
        for name, values in data["sets"].items():
            mark = "  (last used)" if name == data["last"] else ""
            print(f"  {name}: {', '.join(f'{v:g}' for v in values)} Ohm{mark}")
        return

    if args.resistors:
        try:
            values = [float(v) for v in args.resistors.split(",")]
        except ValueError:
            raise SystemExit("Resistor values must be numbers, e.g. -r 1,2.2,4.7,10")
        if not 4 <= len(values) <= 8 or min(values) <= 0:
            raise SystemExit("Give 4 to 8 positive resistor values.")
    set_name, resistors = choose_resistors(args)
    print(f"Resistors ({set_name}): {', '.join(f'{v:g}' for v in resistors)} Ohm")

    tester = Tester(args.host)
    info = tester.get("/api/cal/info")
    print(f"Tester '{info['name']}'  hw_rev{info['hw_rev']}  fw {info['fw']}  MAC {info['mac']}")
    if args.path not in info["paths"]:
        raise SystemExit(f"Unknown path {args.path}; the tester offers {', '.join(info['paths'])}")
    prev = info["active_model"]
    socket = info.get("sockets", {}).get(args.path, "A")  # FIE socket letter for this path

    tester.post("/api/cal/begin", {"path": args.path})
    stop = threading.Event()
    threading.Thread(target=keep_alive, args=(tester, stop), daemon=True).start()
    try:
        for _ in range(50):
            if tester.get("/api/cal/info").get("active"):
                break
            status("  Waiting for the tester to enter calibration mode (unplug any cord or weapon) ...")
            time.sleep(POLL_S)
        else:
            raise SystemExit("\nThe tester did not enter calibration mode. Unplug everything and retry.")
        print("\nThe tester shows a blue C: calibration mode.\n")

        print("Step 1: remove everything from the tester.")
        open_s = wait_for(tester, lambda s: s.get("open"), "Open circuit")
        print(f"  Captured open circuit: V_top {open_s['v_top_mv']:.1f} mV\n")

        points, raw = [], []
        for i, r in enumerate(resistors, 1):
            print(f"Step {i + 1}: connect the {r:g} Ohm resistor between socket {socket} on top and on the bottom.")
            s = wait_for(tester, lambda s: not s.get("open"), f"{r:g} Ohm")
            guess = closest(s["r_est_ohm"], resistors)
            if guess != r and abs(s["r_est_ohm"] - guess) < abs(s["r_est_ohm"] - r):
                print(f"  This looks like the {guess:g} Ohm resistor. Check it, then press Enter to keep "
                      f"this reading or type r to measure again.")
                if input("  > ").strip().lower() == "r":
                    print(f"  Remove the resistor, then connect the {r:g} Ohm resistor again.")
                    wait_until_open(tester)
                    s = wait_for(tester, lambda s: not s.get("open"), f"{r:g} Ohm")
            print(f"  Captured: V_diff {s['v_diff_mv']:.2f} mV")
            points.append({"r_ohm": r, "v_diff_mv": s["v_diff_mv"]})
            point = [r, s["v_top_mv"], s["v_bottom_mv"], s["v_diff_mv"], s["noise_sd_mv"]]
            if "v_diff_rev_mv" in s:
                point += [s["v_top_rev_mv"], s["v_diff_rev_mv"]]
            raw.append(point)
            wait_until_open(tester)
            print()

        fit = tester.post("/api/cal/fit", {"path": args.path,
                                           "open": {"v_high_mv": open_s.get("v_high_mv", open_s["v_top_mv"])},
                                           "points": points})
        print("Result      R ref    R new   error   (factory  previous)")
        for p in fit["points"]:
            print(f"         {p['r_ohm']:7.3f}  {p['r_est_ohm']:7.3f}  {p['err_pct']:5.2f} %"
                  f"   ({p['err_pct_factory']:5.2f} %  {p['err_pct_active']:5.2f} %)")
        verdict = fit["verdict"]
        print(f"\nVerdict: {verdict.upper()}  (worst point {fit['fit']['max_err_pct']:.2f} %, "
              f"target {fit['target_pct']:g} %, limit {fit['limit_pct']:g} %"
              + (f"; above {fit['high_from_ohm']:g} Ohm {fit['target_pct_high']:g} % and {fit['limit_pct_high']:g} %"
                 if "high_from_ohm" in fit else "") + ")")

        run_id = int(time.time())
        saved = False
        if verdict == "fail":
            print("Not saved: a point is outside the limit. Check the resistors and contacts and run again.")
        elif args.yes or input("Save this calibration on the tester? [y/N] ").strip().lower() == "y":
            tester.post("/api/cal/save", {"run_id": run_id})
            saved = True
            print("Saved. The tester uses the new calibration now.")

        m = fit["model"]
        record = {
            # fmt 2: pts[3] and open[2] are the bidirectional average (fmt 1: forward only)
            "id": run_id, "ts": run_id, "fmt": 2, "kind": "calibrate", "set": set_name,
            "path": args.path, "mac": info["mac"], "name": info["name"], "hw": info["hw_rev"], "fw": info["fw"],
            "open": [open_s["v_top_mv"], open_s["v_bottom_mv"], open_s["v_diff_mv"]]
                    + ([open_s["v_top_rev_mv"], open_s["v_diff_rev_mv"]] if "v_diff_rev_mv" in open_s else []),
            # [r_ohm, v_top_mv, v_bottom_mv, v_diff_mv, noise_sd_mv(, reversed v_top_mv, reversed v_diff_mv)]
            "pts": raw,
            "model": "m2s",  # fit/prev: [v_gpio_mv, Rs, Ri, driver slope Ohm/A]
            "fit": [m["v_gpio_mv"], m["r1_r2_ohm"], m["r_internal_ohm"], m.get("driver_slope_ohm_per_a", 0)],
            "prev": [prev["v_gpio_mv"], prev["r1_r2_ohm"], prev["r_internal_ohm"], prev.get("driver_slope_ohm_per_a", 0)],
            "max_err": [fit["fit"]["max_err_pct"], fit["factory"]["max_err_pct"], fit["active"]["max_err_pct"]],
            "verdict": verdict, "saved": saved,
        }
        with open(args.runs_file, "a") as f:
            f.write(json.dumps(record) + "\n")
        try:
            tester.post("/api/cal/run", record)
            where = f"{args.runs_file} and on the tester"
        except SystemExit as e:
            where = f"{args.runs_file} (not on the tester: {str(e).strip()})"
        print(f"Run recorded in {where}.")
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        stop.set()
        tester.post("/api/cal/end")
        print("The tester is back in normal mode.")


if __name__ == "__main__":
    main()
