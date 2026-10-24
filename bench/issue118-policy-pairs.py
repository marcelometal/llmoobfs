#!/usr/bin/env python3
"""Validate and summarize archived, alternating issue-118 VM pairs."""

import argparse
import json
import random
import re
import statistics
import tarfile
from pathlib import Path


def fail(message):
    raise SystemExit(message)


def text_member(archive, name):
    member = archive.getmember(name)
    source = archive.extractfile(member)
    if source is None:
        fail(f"not a regular file: {name}")
    return source.read().decode("utf-8"), member.mtime


def fields(line):
    return dict(re.findall(r"([a-z_]+)=([^\s]+)", line))


def read_run(archive, name, module, files, delay, shape, seed):
    environment, _ = text_member(archive, f"{name}/environment.txt")
    console, mtime = text_member(archive, f"{name}/console.log")
    settings = dict(line.split("=", 1) for line in environment.splitlines() if "=" in line)
    for key, value in (("module", module), ("files", str(files)),
                       ("read_delay_ms", str(delay)), ("shape", shape),
                       ("seed_run", seed)):
        if settings.get(key) != value:
            fail(f"{name}: {key} does not match the requested run")
    hashes = re.findall(r"^([0-9a-f]{64})  .*?/lower\.img$", environment, re.M)
    if len(hashes) != 1:
        fail(f"{name}: expected one input-disk hash")
    if console.count("MOOBFS_POLICY_PASS:") != 1 or "MOOBFS_POLICY_FAIL:" in console:
        fail(f"{name}: guest verdict missing or failed")
    results = {}
    for line in console.splitlines():
        if not line.startswith("MOOBFS_POLICY_RESULT:"):
            continue
        item = fields(line)
        condition = item.get("condition")
        if condition not in ("direct", "delayed") or condition in results:
            fail(f"{name}: unexpected or duplicate condition")
        expected = "denied" if module == "current" else "full"
        count = "777" if module == "current" else "0"
        if item.get("outcome") != expected or item.get("count") != count:
            fail(f"{name}: unexpected QUERY2 verdict")
        results[condition] = {key: int(item[key]) for key in
                              ("wall_ns", "user_us", "sys_us", "read_bytes",
                               "dev_read_sectors")}
    if set(results) != {"direct", "delayed"}:
        fail(f"{name}: direct or delayed query missing")
    return hashes[0], mtime, results


def summarize(current, prelimit, scale):
    left = [value / scale for value in current]
    right = [value / scale for value in prelimit]
    differences = [a - b for a, b in zip(left, right)]
    rng = random.Random(118)
    boot = sorted(statistics.median(rng.choices(differences, k=len(differences)))
                  for _ in range(20000))
    return {
        "current_median": round(statistics.median(left), 3),
        "prelimit_median": round(statistics.median(right), 3),
        "paired_current_minus_prelimit_median": round(statistics.median(differences), 3),
        "paired_bootstrap_95_percent": [round(boot[499], 3), round(boot[19499], 3)],
        "current_lower_pairs": sum(value < 0 for value in differences),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("prefix")
    parser.add_argument("pairs", type=int)
    parser.add_argument("files", type=int)
    parser.add_argument("read_delay_ms", type=int)
    parser.add_argument("shape", choices=("plain", "six"))
    parser.add_argument("seed_run")
    args = parser.parse_args()
    if not 1 <= args.pairs <= 30:
        fail("pair count out of range")
    runs = {}
    hashes = set()
    with tarfile.open(args.archive, "r:gz") as archive:
        for number in range(1, args.pairs + 1):
            times = {}
            for module in ("current", "prelimit"):
                name = f"{args.prefix}-p{number:02d}-{module}"
                digest, mtime, result = read_run(
                    archive, name, module, args.files, args.read_delay_ms,
                    args.shape, args.seed_run)
                hashes.add(digest)
                runs[number, module] = result
                times[module] = mtime
            first = "current" if number % 2 else "prelimit"
            second = "prelimit" if number % 2 else "current"
            if times[first] >= times[second]:
                fail(f"pair {number}: console timestamps do not show alternating order")
    if len(hashes) != 1:
        fail("cloned input images did not have the same hash")
    report = {"pairs": args.pairs, "same_input_image": True,
              "input_sha256": next(iter(hashes)), "conditions": {}}
    metrics = {
        "wall_ms": (lambda r: r["wall_ns"], 1_000_000),
        "process_cpu_ms": (lambda r: r["user_us"] + r["sys_us"], 1_000),
        "read_bytes": (lambda r: r["read_bytes"], 1),
        "device_read_sectors": (lambda r: r["dev_read_sectors"], 1),
    }
    for condition in ("direct", "delayed"):
        report["conditions"][condition] = {}
        for label, (measure, scale) in metrics.items():
            current = [measure(runs[n, "current"][condition])
                       for n in range(1, args.pairs + 1)]
            prelimit = [measure(runs[n, "prelimit"][condition])
                        for n in range(1, args.pairs + 1)]
            report["conditions"][condition][label] = summarize(current, prelimit, scale)
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
