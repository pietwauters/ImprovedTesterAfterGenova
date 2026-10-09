#!/usr/bin/env python3
"""Analyse calibration run records (cal_runs.jsonl, or a browser export).

    python3 tools/cal_analyze.py                         # cal_runs.jsonl in this folder
    python3 tools/cal_analyze.py tester-calibration-2026-10-08.json

For every run it refits the model the way the firmware does (model M2 with an
internal series resistance, tolerance-weighted least squares, V_gpio =
open-circuit high side) and prints the error per resistor (in % of max(R, 1 Ohm)). Runs recorded with the reversed half are fitted three ways:
forward only, reversed only and the average of both. The firmware calibrates on
the average (bidirectional readings) since run record format 2.
"""

import json
import math
import sys


# ---- Model M2 (firmware): internal series resistance Ri between the sense points, and a series
# resistance outside them that grows with the current (GPIO drivers): Rs_eff = Rs + s * I.
#   V = Vg (R + Ri) / (R + Ri + Rs_eff)        R = Rs_eff V / (Vg - V) - Ri
# s is a fixed constant (DRIVER_SLOPE, same for every tester); only Rs and Ri are fitted.
# Errors are judged and fitted relative to max(R, 1 Ohm), against a target of 2 % up to 10 Ohm and
# 5 % above (limits 5 % and 10 %), exactly as the firmware does.

TARGET_LOW, LIMIT_LOW, TARGET_HIGH, LIMIT_HIGH, HIGH_FROM = 2.0, 5.0, 5.0, 10.0, 10.0
DRIVER_SLOPE = 400.0  # Ohm/A: GPIO driver resistance rises with the current


def m2_rs_eff_from_v(m, v):
    """Rs_eff for a measured V: I = (Vg - V)/Rs_eff and Rs_eff = Rs + s I -> Rs_eff^2 - Rs Rs_eff - s (Vg - V) = 0."""
    g, rs, ri = m[:3]
    s = m[3] if len(m) > 3 else 0.0
    return (rs + math.sqrt(rs * rs + 4 * s * (g - v))) / 2


def m2_volt(m, r):
    """V for a resistance: x = R + Ri, I = Vg/(x + Rs_eff) -> Rs_eff^2 + (x - Rs) Rs_eff - (Rs x + s Vg) = 0."""
    g, rs, ri = m[:3]
    s = m[3] if len(m) > 3 else 0.0
    x = r + ri
    rse = (-(x - rs) + math.sqrt((x - rs) ** 2 + 4 * (rs * x + s * g))) / 2
    return g * x / (x + rse)


def m2_res(m, v, clamp=True):
    g = m[0]
    if v >= g:
        return -1.0
    r = m2_rs_eff_from_v(m, v) * v / (g - v) - m[2]
    return max(0.0, r) if clamp else r


def scaled_error(est, r):
    """Error in % of max(R, 1 Ohm): below 1 Ohm it counts in ohms (2 % = 20 mOhm)."""
    return (est - r) / max(r, 1.0) * 100


def target(r):
    return TARGET_HIGH if r > HIGH_FROM else TARGET_LOW


def m2_errors(m, R, V):
    return [scaled_error(m2_res(m, v, clamp=False), r) for r, v in zip(R, V)]


def fit_m2(R, V, vg, slope=DRIVER_SLOPE):
    """Least squares on the tolerance-weighted error over (Rs, Ri); V_gpio and the driver slope fixed."""
    def cost(x):
        e = m2_errors((vg, x[0], x[1], slope), R, V)
        return sum((ei / target(r)) ** 2 for ei, r in zip(e, R))
    seeds = sorted(r * (vg - v) / v for r, v in zip(R, V) if 0 < v < vg)
    i_typ = vg / (seeds[len(seeds) // 2] + 3)  # rough current, to start Rs below the plain-divider value
    x = nelder_mead(cost, [seeds[len(seeds) // 2] - slope * i_typ, 0.0], [2.0, 0.05])
    x = nelder_mead(cost, x, [0.2, 0.005])
    return (vg, x[0], x[1], slope)


def m2_from_m0(m):
    """An M0 model (Rs, c) as M2 without driver slope: Ri = -c / Rs."""
    g, rs, c = m
    return (g, rs, -c / rs, 0.0)


def volt(m, r):
    g, k, c = m
    return g * r / (r + k + c / r)


def res(m, v):
    g, k, c = m
    if v <= 0 or v >= g:
        return -1.0
    a, b, cc = v - g, v * k, v * c
    d = b * b - 4 * a * cc
    if d < 0:
        return -1.0
    q = -0.5 * (b + math.copysign(math.sqrt(d), b))
    roots = [x for x in (q / a, cc / q) if x > 0]
    return max(roots) if roots else -1.0


def errors(m, R, V):
    return [((res(m, v) - r) / r * 100) if res(m, v) > 0 else 1000.0 for r, v in zip(R, V)]


def nelder_mead(f, x0, step, iters=2000, tol=1e-12):
    n = len(x0)
    pts = [list(x0)] + [[x0[j] + (step[j] if j == i else 0) for j in range(n)] for i in range(n)]
    vals = [f(p) for p in pts]
    for _ in range(iters):
        order = sorted(range(n + 1), key=lambda i: vals[i])
        pts, vals = [pts[i] for i in order], [vals[i] for i in order]
        if vals[-1] - vals[0] < tol:
            break
        cen = [sum(p[j] for p in pts[:-1]) / n for j in range(n)]
        xr = [2 * cen[j] - pts[-1][j] for j in range(n)]
        fr = f(xr)
        if fr < vals[0]:
            xe = [3 * cen[j] - 2 * pts[-1][j] for j in range(n)]
            fe = f(xe)
            pts[-1], vals[-1] = (xe, fe) if fe < fr else (xr, fr)
        elif fr < vals[-2]:
            pts[-1], vals[-1] = xr, fr
        else:
            xc = [(cen[j] + pts[-1][j]) / 2 for j in range(n)]
            fc = f(xc)
            if fc < vals[-1]:
                pts[-1], vals[-1] = xc, fc
            else:
                pts = [pts[0]] + [[(pts[0][j] + p[j]) / 2 for j in range(n)] for p in pts[1:]]
                vals = [vals[0]] + [f(p) for p in pts[1:]]
    return pts[0]


def fit(R, V, vg):
    """Least squares on the relative resistance error over (R1_R2, Correction), V_gpio fixed."""
    cost = lambda x: sum(e * e for e in errors((vg, x[0], x[1]), R, V))
    best = None
    for start in ([100.0, 0.0], [90.0, -10.0], [120.0, 20.0]):
        x = nelder_mead(cost, start, [5.0, 5.0])
        if best is None or cost(x) < cost(best):
            best = x
    return (vg, best[0], best[1])


def leave_one_out(R, V, vg):
    worst = 0.0
    for i in range(len(R)):
        m = fit_m2(R[:i] + R[i + 1:], V[:i] + V[i + 1:], vg)
        worst = max(worst, abs(m2_errors(m, [R[i]], [V[i]])[0]))
    return worst


def load(path):
    with open(path) as f:
        text = f.read()
    try:
        data = json.loads(text)
        if isinstance(data, list):
            return data
        return [data] if "pts" in data else data.get("runs", [])  # one-line .jsonl, or a browser export
    except ValueError:
        return [json.loads(line) for line in text.splitlines() if line.strip()]


def report(name, R, V, vg):
    m = fit_m2(R, V, vg)
    e = m2_errors(m, R, V)
    print(f"  {name:10s} worst {max(map(abs, e)):5.2f} %   rms {math.sqrt(sum(x * x for x in e) / len(e)):5.2f} %"
          f"   left-out point {leave_one_out(R, V, vg):5.2f} %   errors " + " ".join(f"{x:+6.2f}" for x in e))
    return m


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "cal_runs.jsonl"
    runs = load(path)
    if not runs:
        raise SystemExit(f"No runs in {path}")
    validations = [r for r in runs if r.get("kind") == "validate"]
    for run in runs:
        if run.get("kind") == "validate":
            continue
        pts = run["pts"]
        R = [p[0] for p in pts]
        name = f" '{run['name']}'" if run.get("name") else ""
        print(f"\nRun {run.get('id')}  tester {run.get('mac')}{name}  hw_rev{run.get('hw')}  set '{run.get('set')}'  "
              f"path {run.get('path')}  saved {run.get('saved')}")
        print("  resistors  " + " ".join(f"{r:6.2f}" for r in R) + " Ohm")
        op = run["open"]
        fwd = [(p[1] - p[2]) / 1000 for p in pts]  # forward half (in fmt 2 records p[3] is the average)
        report("forward", R, fwd, op[0] / 1000)
        if len(op) >= 5 and all(len(p) >= 7 for p in pts):
            # reversed half: same channels, the bottom one is high; high side = v_top_rev + v_diff_rev
            rev = [p[6] / 1000 for p in pts]
            vg_rev = (op[3] + op[4]) / 1000
            report("reversed", R, rev, vg_rev)
            report("average", R, [(a + b) / 2 for a, b in zip(fwd, rev)], (op[0] / 1000 + vg_rev) / 2)
            if run.get("contact"):
                print("  contact check (captures, spread %)  " + "  ".join(f"{c[0]}/{c[1]:.2f}" for c in run["contact"]))
            print("  forward - reversed V_diff  " + " ".join(f"{(p[1] - p[2] - p[6]):+6.2f}" for p in pts) + " mV")
    if validations:
        report_validations(validations)


def report_validations(runs):
    """Runs of tools/cal_validate.py: the tester's (A-A) model read on other connections."""
    print("\nOther connections, read with the tester's model (signed error per resistor):")
    for run in runs:
        R = [p[0] for p in run["pts"]]
        V = [p[3] / 1000 for p in run["pts"]]  # bidirectional average
        a, o = run["active"], run["own"]
        if run.get("model") in ("m2", "m2s"):
            e = m2_errors((a[0] / 1000, *a[1:]), R, V)
            own = m2_errors((o[0] / 1000, *o[1:]), R, V)
        else:  # older records: the Rs + c/R model the tester used then
            e = errors((a[0] / 1000, a[1], a[2]), R, V)
            own = errors((o[0] / 1000, o[1], o[2]), R, V)
        ends = " - ".join(run.get("ends", ["?", "?"]))
        name = f" '{run['name']}'" if run.get("name") else ""
        print(f"  {run.get('mac', '')[-5:]}{name} {run['path']:6s} {ends:20s} worst {max(e, key=abs):+6.2f} %  "
              f"rms {math.sqrt(sum(x * x for x in e) / len(e)):4.2f} %  own model worst {max(map(abs, own)):4.2f} %  "
              f"errors " + " ".join(f"{x:+6.2f}" for x in e) + f"   ({run.get('use', '')})")


if __name__ == "__main__":
    main()
