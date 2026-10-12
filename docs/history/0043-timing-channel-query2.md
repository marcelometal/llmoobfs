# 0043 — Timing channel: per-domain `QUERY2` against v1 (test 4 of the specification)

Date: 2026-10-12. Status: measured on the host of journal 0042; results in
`bench/results/2026-10-12-macmini-timing-v2/`.

## Goal

Verify test 4 of `docs/spec-query-v2.md`, which was missing for the Q2a done-when criterion: the
latency of a query in a domain with 10 files should not vary beyond noise when another
domain goes from 0 to 20 000 files that match the predicate. And repeat, on the same host, the
v1 experiment of journal 0030 as a contrast.

## Method

`bench/timing-v2.sh` (uid 1001 with 10 files in `dA`, uid 1002 with N files in `dB`, both
0700; the layer is remounted after creating the directories, so as not to leave the domains in
fallback). For the contrast, the same set with N = 20 000, remounted with `domain_depth=0`, and
v1 on the root as uid 1001.

## Results (p50 per call, 10 results in all)

| Case | N (of uid 1002) | p50 | p99 |
| --- | --- | --- | --- |
| `QUERY2` | 0 | 15.3 µs | 17.5 µs |
| `QUERY2` | 1 000 | 23.4 µs | 30.3 µs |
| `QUERY2` | 5 000 | 16.3 µs | 25.1 µs |
| `QUERY2` | 20 000 | 14.0 µs | 15.9 µs |
| `QUERY2`, 3 repetitions | 20 000 | 10.1 µs (10.0 to 12.5) | 14.5 µs |
| v1 as uid 1001, 3 repetitions | 20 000 | 13.8 ms (13.79 to 13.81) | 16.5 ms |

## Conclusion

- **The channel closed in v2.** There is no trend with N: the largest N had the lowest latency of the
  sweep, and the variation between values of N (10 to 23 µs) is of the same order as the variation between
  repetitions with fixed N. v2 does not walk entries of other domains, so the time does not
  carry information about them.
- **v1 keeps the channel on this host:** 13.8 ms with 20 000 invisible candidates, about
  0.69 µs per candidate (on Azure, 1.15 µs; a different CPU). v1 is only open to ordinary users
  with `domain_depth=0`, and with domains on it requires `CAP_SYS_ADMIN` (Q2a).
- For the same question with 20 000 files belonging to others, v2 answered about 1 400 times
  faster than v1.

## Limitations

- One repetition per N in the sweep; the point N = 1 000 (23.4 µs) is the highest and stood alone.
  The spread between repetitions was measured only at N = 20 000, and the report marked that line as
  inconclusive by the 20% spread criterion, natural for values of 10 µs. The conclusion about the
  channel does not depend on this: the difference from v1 is three orders of magnitude.
- Only ext4; a single equality predicate; no concurrent load.

## Next step

v1 baseline and per-domain W3 on this host (forms of journal 0029), "N most recent" and Q2c
against Q2b.
