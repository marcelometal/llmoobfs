# Write-side cost of a side index — Mac mini, 2026-10-13

What it costs, at write time, to keep the structured fields of a memory queryable: nothing, typed
attributes without the layer, typed attributes through the layer, a row in a side SQLite as
durable as the file, and the same with `synchronous=NORMAL`. Host and module as in
`../2026-10-13-macmini-q2-posix/`. Analysis: journal 0088, section 2.4.

Command, per file system, mode and repetition (caches dropped before each):
`bench/synccost --dir <empty dir> --mode files|xattr|sqlite [--sync full|normal] --n 3000`.
"layer" runs used a directory on the layer mount; "plain" ones a directory on the same partition.

| File | Content |
| --- | --- |
| `results.txt` | 45 lines: repetition, file system, where, mode, p50, p95 and mean in µs per memory, total seconds |
| `environment.txt` | kernel and module version |

Each memory is published with a temporary file, `fsync`, rename and `fsync` of the directory, so
every figure is dominated by two to three device flushes on a SATA SSD.
