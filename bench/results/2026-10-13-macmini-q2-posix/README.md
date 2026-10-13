# Latency per operation, layer against the bare lower — Mac mini, 2026-10-13

M4 question 2 on the current module. Host: journal 0042 (Mac mini Server 2011, Debian 13, kernel
`6.12.111+deb13-amd64`, one SATA SSD with ext4, btrfs and XFS partitions). Module: built from
commit `db8f75b` (the layer code of `v0.3.0-experimental`; `modinfo` version `0.2.0+dev`).
Analysis: journal 0088, section 2.1.

Command: `sudo env BIN=build/host sh bench/run.sh --out <dir> --scale 10000 --profile 8 --reps 3
--plain A-<fs>=<plain dir> --layer C-<fs>=<mount>:<lower dir>` for the three file systems in one
run. Each layer was a fresh mount over an empty directory with `domain_depth=0`; each plain
directory was on the same partition as its layer's lower.

| File | Content |
| --- | --- |
| `report.txt` | per system and workload: n, errors, p50, p99; W1 create, W2 read, W4 attribute set and removal, W8 mixed, W3 queries, W5, W6 and W7 mounts for the layers |
| `environment.txt` | kernel, CPU, memory, SQLite version, parameters |
| `run.log` | the run, with the equivalence verdict of each repetition |

The raw records (`rep*/`) are kept on the host and are not in the repository. 43 lines of the
report carry the harness's `INCONCLUSIVE` mark.
