# 0031 — Remount time with 25 000 to 60 000 memories

Date: 2026-10-11. Status: measured (pilot level).

## Goal

Complete the W7 that produced no value at the large scales of the series in journal 0029 and also
measure W6 (cold cache), to inform the decision, left to M4 by ADR-0007, of whether or not to
persist the index on disk.

## Result (Azure host, ext4 on P6, evidence in `bench/results/2026-10-11-azure-remount/`)

| Memories | W7, warm cache (median of 5) | W6, cold cache (median of 3) | Accounted index |
| --- | --- | --- | --- |
| 25 000 | 0.47 s | 49 s | 14.9 MB |
| 40 000 | 0.79 s | 75 s | 23.9 MB |
| 60 000 | 1.18 s | 126 s | 35.8 MB |

With a warm cache, rebuilding the index costs about 19.7 µs per memory, linear, as at the smaller
scales (journal 0029). With a cold cache, about 2 ms per memory: the scan reads the inode and the
attributes of each file with random reads, and the P6 disk, probably already out of burst
credits after the series, delivers a few hundred per second. W6 depends on the disk and is an
upper bound for this disk, not a property of the layer alone.

## Reading

- Mounting after rebooting the machine, with 60 000 memories, took more than two minutes on this
  disk, and the time grows linearly with the number of memories. Until then, no query is
  answered.
- It is the first concrete number for the question left open by ADR-0007 about persisting the
  index: with a persisted index, the cold mount would read a sequential file of about 36 MB
  instead of 60 000 random reads. The cost would be keeping that file coherent with the lower
  (including after changes made outside the mount and after crashes), which needs a decision of
  its own.
- In alternative E of ADR-0009 (index partitioned by domain), the rebuild could be done per
  domain and on demand, at the first query to each domain, which reduces the time to the first
  response without persisting anything; worth measuring before persisting.

## Limitations

One host, one disk with bursting in an unknown state, three cold measurements per scale. Repeat
with a fixed-IOPS disk before any conclusion about persistence.
