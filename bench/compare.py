# SPDX-License-Identifier: GPL-2.0-only
"""bench/compare: tests whether two systems differ, per workload and operation.

Usage: compare.py --out DIR --a SYSTEM REP_DIR [REP_DIR ...] --b SYSTEM REP_DIR [REP_DIR ...]

REP_DIRs are repetitions in the layout bench/report.py reads (<system>/<prefix>.<proc>.rec).
A and B may be two systems of the same repetitions (A-ext4 against C-ext4) or the same system
in two sets of repetitions (two module builds). For every metric in METRICS the unit of
observation is one repetition's value (its p50, p95 or p99 over all calls), so calls within a
repetition, which are not independent, are never counted as samples.

Per (workload, op, metric) the report gives:
- the median of A and of B across repetitions and their relative difference (B - A) / A;
- a 95 % confidence interval of that difference, percentile bootstrap over repetitions
  (Efron 1979), 10 000 resamples, fixed seed so a report can be reproduced;
- the two-sided p-value of the Mann-Whitney U test (normal approximation with the tie
  correction; exact tables are not used, so with fewer than 8 repetitions per side the
  p-value is only indicative);
- a verdict: "differs" when the interval excludes 0 and p < 0.05, "no difference detected"
  otherwise, and "too few repetitions" below 5 per side. The method asks for 30
  (docs/benchmark-plan.md, "Method"); fewer are flagged.

Also writes ecdf.csv: the empirical distribution of every call's duration, pooled over the
repetitions of each side, at every 0.1 % (latency has a tail that a median hides). Standard
library only.
"""
import argparse
import csv
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from report import OPS, percentile, read_records, summarize_rep  # noqa: E402

METRICS = ("p50_us", "p95_us", "p99_us")
RESAMPLES = 10000
SEED = 20261019
WANTED_REPS = 30


def mann_whitney_p(a, b):
    """Two-sided p-value of the Mann-Whitney U test, normal approximation, tie-corrected."""
    n1, n2 = len(a), len(b)
    allv = sorted([(v, 0) for v in a] + [(v, 1) for v in b])
    ranks = [0.0] * len(allv)
    ties = 0.0
    i = 0
    while i < len(allv):
        j = i
        while j + 1 < len(allv) and allv[j + 1][0] == allv[i][0]:
            j += 1
        r = (i + j) / 2.0 + 1
        for k in range(i, j + 1):
            ranks[k] = r
        t = j - i + 1
        ties += t ** 3 - t
        i = j + 1
    r1 = sum(r for r, (_, side) in zip(ranks, allv) if side == 0)
    u1 = r1 - n1 * (n1 + 1) / 2.0
    n = n1 + n2
    mu = n1 * n2 / 2.0
    var = n1 * n2 / 12.0 * ((n + 1) - ties / (n * (n - 1)))
    if var <= 0:
        return 1.0
    z = (abs(u1 - mu) - 0.5) / math.sqrt(var)  # continuity correction
    if z < 0:
        z = 0.0
    return math.erfc(z / math.sqrt(2))


def rel_diff(a, b):
    ma = percentile(sorted(a), 50)
    mb = percentile(sorted(b), 50)
    return (mb - ma) / ma if ma else None


def bootstrap_ci(a, b, rng):
    diffs = []
    for _ in range(RESAMPLES):
        ra = [a[rng.randrange(len(a))] for _ in a]
        rb = [b[rng.randrange(len(b))] for _ in b]
        d = rel_diff(ra, rb)
        if d is not None:
            diffs.append(d)
    diffs.sort()
    return percentile(diffs, 2.5), percentile(diffs, 97.5)


def side_values(reps, system):
    """{(workload, op): {metric: [one value per repetition]}} for one system."""
    out = {}
    for rep in reps:
        for (sysname, wl, op), m in rep.items():
            if sysname != system:
                continue
            d = out.setdefault((wl, op), {k: [] for k in METRICS})
            for k in METRICS:
                if m[k] is not None:
                    d[k].append(m[k])
    return out


def pooled_calls(rep_dirs, system):
    """{(workload, op): sorted durations in µs of every successful call}."""
    out = {}
    for rep in rep_dirs:
        sdir = os.path.join(rep, system)
        if not os.path.isdir(sdir):
            continue
        for name in sorted(os.listdir(sdir)):
            if not name.endswith(".rec"):
                continue
            for wl, op, _start, dur, _proc, result in read_records(os.path.join(sdir, name)):
                if result < 0:
                    continue
                base, shape = op & 0xFF, op >> 8
                opname = OPS.get(base, str(base)) + (f":Q{shape}" if shape else "")
                out.setdefault((f"W{wl}", opname), []).append(dur / 1000.0)
    for v in out.values():
        v.sort()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--a", nargs="+", required=True, metavar="ARG", help="SYSTEM REP_DIR...")
    ap.add_argument("--b", nargs="+", required=True, metavar="ARG", help="SYSTEM REP_DIR...")
    args = ap.parse_args()
    if len(args.a) < 2 or len(args.b) < 2:
        sys.exit("compare: --a and --b each take a system name and at least one repetition directory")
    sa, da = args.a[0], args.a[1:]
    sb, db = args.b[0], args.b[1:]
    va = side_values([summarize_rep(d) for d in da], sa)
    vb = side_values([summarize_rep(d) for d in db], sb)
    keys = sorted(set(va) & set(vb))
    if not keys:
        sys.exit(f"compare: no workload and operation common to {sa} and {sb}")
    rng = random.Random(SEED)
    rows = []
    for key in keys:
        for metric in METRICS:
            a, b = va[key][metric], vb[key][metric]
            row = {"workload": key[0], "op": key[1], "metric": metric, "reps_a": len(a), "reps_b": len(b)}
            if len(a) < 5 or len(b) < 5:
                row["verdict"] = "too few repetitions"
            else:
                row["median_a"] = percentile(sorted(a), 50)
                row["median_b"] = percentile(sorted(b), 50)
                row["rel_diff"] = rel_diff(a, b)
                row["ci_low"], row["ci_high"] = bootstrap_ci(a, b, rng)
                row["p_value"] = mann_whitney_p(a, b)
                excl = row["ci_low"] > 0 or row["ci_high"] < 0
                row["verdict"] = "differs" if excl and row["p_value"] < 0.05 else "no difference detected"
            if min(len(a), len(b)) < WANTED_REPS:
                row["verdict"] += f" (fewer than {WANTED_REPS} repetitions)"
            rows.append(row)
    os.makedirs(args.out, exist_ok=True)
    fields = ["workload", "op", "metric", "reps_a", "reps_b", "median_a", "median_b", "rel_diff",
              "ci_low", "ci_high", "p_value", "verdict"]
    with open(os.path.join(args.out, "compare.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow({k: (f"{r[k]:.4f}" if isinstance(r.get(k), float) else r.get(k, "")) for k in fields})
    ca, cb = pooled_calls(da, sa), pooled_calls(db, sb)
    with open(os.path.join(args.out, "ecdf.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["workload", "op", "system", "fraction", "duration_us"])
        for key in keys:
            for name, calls in ((sa, ca.get(key, [])), (sb, cb.get(key, []))):
                for i in range(1, 1001):
                    if calls:
                        w.writerow([key[0], key[1], name, f"{i / 1000:.3f}", f"{percentile(calls, i / 10):.3f}"])
    for r in rows:
        if "rel_diff" in r:
            print(f"{r['workload']} {r['op']} {r['metric']}: {sa}={r['median_a']:.1f}us {sb}={r['median_b']:.1f}us "
                  f"diff={r['rel_diff'] * 100:+.1f}% [95% CI {r['ci_low'] * 100:+.1f}%, {r['ci_high'] * 100:+.1f}%] "
                  f"p={r['p_value']:.3g} n={r['reps_a']}/{r['reps_b']}: {r['verdict']}")
        else:
            print(f"{r['workload']} {r['op']} {r['metric']}: n={r['reps_a']}/{r['reps_b']}: {r['verdict']}")


if __name__ == "__main__":
    main()
