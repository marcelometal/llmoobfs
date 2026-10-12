# ai-memory stage 1a on the Mac mini (2026-10-12)

ai-memory v2.6.3 (385b81ea), `cargo test --release --locked -p ai-memory-wiki -p ai-memory-store
-p ai-memory-workstream`, with `TMPDIR` on plain ext4 and inside the layer over ext4 on the same SSD
(`bench/client/stage1a-tests.sh`). Kernel 6.12.111 (Debian 13).

Module: branch `fix/rename-over-deleted-name` (e82ab64). With the module of `main` the layer run had
one failure (`wiki::tests::backfill_then_reindex_rebuilds_from_wiki_alone`, rename over a
just-deleted name returned `EINVAL`; journal 0057). With the fix:

| run | passed | failed | ignored | seconds |
|-----|-------:|-------:|--------:|--------:|
| plain ext4 | 1 147 | 0 | 2 | 297 |
| layer over ext4 | 1 147 | 0 | 2 | 316 |

`*.results.txt` holds the per-binary `test result` lines; `summary.txt` the exit status and wall
time. Wall time includes tests with sleeps and is not a performance measurement.
