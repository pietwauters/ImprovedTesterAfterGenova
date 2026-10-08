#!/usr/bin/env python3
"""Analyse calibration run records (cal_runs.jsonl, or a browser export).

    python3 tools/cal_analyze.py                         # cal_runs.jsonl in this folder
    python3 tools/cal_analyze.py tester-calibration-2026-10-08.json

For every run it refits the model the way the firmware does (least squares on
the relative resistance error, V_gpio = open-circuit high side) and prints the
error per resistor. Runs recorded with the reversed half are fitted three ways:
forward only, reversed only and the average of both. The firmware calibrates on
the average (bidirectional readings) since run record format 2.
"""

import json
import math
import sys


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
        m = fit(R[:i] + R[i + 1:], V[:i] + V[i + 1:], vg)
        worst = max(worst, abs(errors(m, [R[i]], [V[i]])[0]))
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
    m = fit(R, V, vg)
    e = errors(m, R, V)
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
        a = run["active"]
        e = errors((a[0] / 1000, a[1], a[2]), R, V)
        o = run["own"]
        own = errors((o[0] / 1000, o[1], o[2]), R, V)
        ends = " - ".join(run.get("ends", ["?", "?"]))
        name = f" '{run['name']}'" if run.get("name") else ""
        print(f"  {run.get('mac', '')[-5:]}{name} {run['path']:6s} {ends:20s} worst {max(e, key=abs):+6.2f} %  "
              f"rms {math.sqrt(sum(x * x for x in e) / len(e)):4.2f} %  own model worst {max(map(abs, own)):4.2f} %  "
              f"errors " + " ".join(f"{x:+6.2f}" for x in e) + f"   ({run.get('use', '')})")


if __name__ == "__main__":
    main()
