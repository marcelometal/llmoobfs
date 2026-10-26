#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Recorded, unbounded G5 numbers from an archived gate run (protocol section 13.7, journal 0201).

The gate computes the bounded numbers: Q1 relevance per branch and the floor. This script
adds what section 13.7 records but the gate did not print: Q2 relevance and the per-type
breakdown of Q1 and Q2. It reads the gate's raw results (the first timed MoobFS run, the
same results the gate scored), its sample and the pinned dataset, and runs no search. Its
numbers cannot pass or fail G5.
"""
import argparse
import collections
import gzip
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from g5_mounted import bootstrap, metrics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gate-dir", required=True, help="directory with sample.json and gate-raw.json.gz")
    ap.add_argument("--data", default="/opt/g5-0197/data/longmemeval_s_cleaned.json")
    args = ap.parse_args()
    with open(os.path.join(args.gate_dir, "sample.json")) as f:
        order = [row[0] for row in json.load(f)["order"]]
    with gzip.open(os.path.join(args.gate_dir, "gate-raw.json.gz"), "rt") as f:
        raw = json.load(f)
    with open(args.data) as f:
        by_id = {x["question_id"]: x for x in json.load(f)}
    picked = [by_id[q] for q in order]
    merged = {}
    for runs in raw["moob"].values():
        merged.update(runs[0]["results"])

    out = {}
    for flt in ("Q1", "Q2"):
        # metrics() scores the "|Q1" entries; hand it the chosen filter's entries under that name.
        view = {k.replace(f"|{flt}", "|Q1"): v for k, v in merged.items() if k.endswith(f"|{flt}")}
        per_q = metrics(picked, view)
        groups = collections.defaultdict(list)
        for row in per_q.values():
            groups["all"].append(row)
            groups[row["type"]].append(row)
        for group, rows in sorted(groups.items()):
            for branch in ("vector", "lexical", "fusion"):
                for m in ("any", "all", "ndcg"):
                    vals = [r[branch][m] for r in rows if r[branch][m] is not None]
                    mean, lo, hi = bootstrap(vals)
                    out.setdefault(flt, {}).setdefault(group, {})[f"{branch}_{m}10"] = \
                        {"mean": round(mean, 3), "ci95": [round(lo, 3), round(hi, 3)], "n": len(vals)}
                    print(f"G5_REPORT {flt} {group} {branch}_{m}10={mean:.3f}[{lo:.3f},{hi:.3f}]n={len(vals)}")
    with open(os.path.join(args.gate_dir, "report.json"), "w") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
