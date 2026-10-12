# 0055 — Measurement gaps: cold cache, mount time and write cost

Date: 2026-10-12. Status: measured on the host of journal 0042; results in
`bench/results/2026-10-12-macmini-gaps/`.

## Goal

Journals 0044 to 0049 measured queries with a warm cache and no writes. This closes three gaps the
maintainer approved: queries right after dropping the page cache, mount time at scale, and the
cost that domains add to writes.

## Results

| Measurement (60 000 memories) | Value |
| --- | --- |
| Mount, page cache dropped | 18.5 to 19.3 s (Azure host, journal 0031: 126 s) |
| Mount, warm cache | 0.73 to 0.74 s |
| v1 Q3 as a user, first call after dropping the cache | 179 ms (warm: 5.4 ms) |
| QUERY2 `namespace AND agent` on `ns3`, first call after dropping the cache | 0.62 ms (warm: 0.24 ms) |
| W1 create with fsync, p50, domains off / on | 12.69 / 12.83 ms |
| W4 attribute set, p50, domains off / on | 15.7 / 15.5 µs |
| W4 attribute remove, p50, domains off / on | 20.1 / 20.6 µs |

## Conclusions

- A cold cache hurts v1 by 33 times (it reads directories from disk to authorize each result) and
  QUERY2 by about 2.6 times (it answers from the index in RAM; what remains is the first touch of the
  dentries for the result paths).
- Cold mount on this SATA SSD is about 0.31 ms per memory, against about 2 ms on the Azure P6 disks;
  the rebuild reads every xattr from disk.
- Domains cost about 1% on file creation (dominated by fsync) and nothing measurable on attribute
  updates.

## Limitations

- The write benchmark directory was created after the mount, so with domains on it ran in fallback
  mode (`fallback_domains=1`); the homogeneity evaluation still ran on every write, so the write
  cost is representative, but not the homogeneous fast path for queries.
- Single calls per repetition for the cold measurements (20 repetitions); the warm single-call
  values are higher than the 50-call means of journal 0044 because each repetition starts a process.
- ext4 only.
