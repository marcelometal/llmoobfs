# SPDX-License-Identifier: GPL-2.0-only
"""Reanalyse confirm40 without loading modules. Input: the preserved m40.tgz archive.

Usage: python3 analyze.py ARCHIVE NEW_OUTPUT_DIRECTORY
Reads archive members in memory, never extracts or changes the archive. Output creation
is exclusive. The unit of resampling is a repetition, never an individual query.
"""
import csv
import hashlib
from pathlib import Path
import random
import re
import struct
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from compare import bootstrap_ci, mann_whitney_p, rel_diff, RESAMPLES, SEED
from report import percentile


def median(v):
    return percentile(sorted(v), 50)


def paired_ci(a, b):
    rng = random.Random(SEED)
    d = []
    for _ in range(RESAMPLES):
        indices = [rng.randrange(len(a)) for _ in a]
        d.append(rel_diff([a[i] for i in indices], [b[i] for i in indices]))
    return percentile(sorted(d), 2.5), percentile(sorted(d), 97.5)


def write_csv(path, rows):
    with path.open('x', newline='') as f:
        w = csv.DictWriter(f, list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: analyze.py ARCHIVE NEW_OUTPUT_DIRECTORY")
    archive, output = Path(sys.argv[1]), Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=False)
    data, runs = {}, []
    with tarfile.open(archive) as t:
        for member in t.getmembers():
            m = re.fullmatch(r'results/(m0102|fix40)/rep(\d+)/(v2-dom|v2-recent)/w3\.0\.rec', member.name)
            if not m:
                continue
            system, rep, shape = m[1], int(m[2]), m[3]
            records = list(struct.iter_unpack('=IIQQIi', t.extractfile(member).read()))
            assert len(records) == (150 if shape == 'v2-dom' else 100)
            for q in range(1, 4 if shape == 'v2-dom' else 3):
                rows = [r for r in records if r[1] >> 8 == q]
                assert len(rows) == 50 and all(r[0] == 3 and r[4] == 0 for r in rows)
                expected = (253 if q < 3 else 3030) if shape == 'v2-dom' else 20
                assert all(r[5] == expected for r in rows), (system, rep, shape, q)
                data[system, rep, shape, q] = [r[3] / 1000 for r in rows]
        log = t.extractfile('results/runs.txt').read().decode()
    order = {}
    for line in log.splitlines():
        m = re.match(r'rep=(\d+) module=(\S+)', line)
        if m:
            order.setdefault(int(m[1]), []).append(m[2])
    assert len(order) == 40 and all(sorted(v) == ['fix40', 'm0084', 'm0102'] for v in order.values())
    shapes = [('v2-dom', 1), ('v2-dom', 2), ('v2-dom', 3), ('v2-recent', 1), ('v2-recent', 2)]
    for system in ('m0102', 'fix40'):
        for rep in range(1, 41):
            for shape, q in shapes:
                v = data[system, rep, shape, q]
                for discard in (0, 1, 5):
                    rest = sorted(v[discard:])
                    runs.append(dict(system=system, rep=rep, position=order[rep].index(system)+1,
                                     shape=shape, query=q, discard_cycles=discard, calls=len(rest),
                                     first_us=v[0], first_is_max=int(v[0] == max(v)),
                                     p50_us=percentile(rest, 50), p99_us=percentile(rest, 99)))
    comparisons = []
    for shape, q in shapes:
        for discard in (0, 1, 5):
            for metric in ('p50_us', 'p99_us'):
                a, b = [[r[metric] for r in runs if r['system'] == system and
                         r['shape'] == shape and r['query'] == q and r['discard_cycles'] == discard]
                        for system in ('m0102', 'fix40')]
                lo, hi = bootstrap_ci(a, b, random.Random(SEED))
                plo, phi = paired_ci(a, b)
                comparisons.append(dict(shape=shape, query=q, discard_cycles=discard, metric=metric,
                                        reps=40, median_a=median(a), median_b=median(b),
                                        delta_us=median(b)-median(a), rel_diff=rel_diff(a,b),
                                        ci_low=lo, ci_high=hi, paired_ci_low=plo, paired_ci_high=phi,
                                        mann_whitney_p=mann_whitney_p(a,b)))
    sensitivity = []
    for shape, q in shapes:
        for selection in ('first20', 'last20', 'rotation0', 'rotation1', 'rotation2'):
            indices = [r for r in range(1,41) if (r <= 20 if selection == 'first20' else
                       r > 20 if selection == 'last20' else r % 3 == int(selection[-1]))]
            a, b = [[median(data[system, r, shape, q]) for r in indices]
                    for system in ('m0102', 'fix40')]
            sensitivity.append(dict(shape=shape, query=q, selection=selection, reps=len(indices),
                                    median_a=median(a), median_b=median(b), rel_diff=rel_diff(a,b)))
    write_csv(output / 'per-repetition.csv', runs)
    write_csv(output / 'comparison.csv', comparisons)
    write_csv(output / 'sensitivity.csv', sensitivity)
    (output / 'input-sha256.txt').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  m40.tgz\n')
    print('Validated 20000 successful calls, 40 matched repetitions; wrote 3 CSV files and archive hash.')


if __name__ == '__main__':
    main()
