# Profile of `QUERY`, shape Q7, on the Azure host (2026-10-11)

Module built from commit `b85d041` (harness of PR 20 plus the `QUERY` fast path, journal 0027).
Dataset: 1000 memories, profile 8, seed 1, written with `bench-load` W1 under
`/srv/moob/layer-ext4/prof`. Query: `score ge int64 250 score le int64 750` (513 results).

- `perf-children.txt`, `perf-self-callers.txt`: `perf record -F 4999 -g` over 3000 executions of
  `build/host/query` (perf 6.17 from Ubuntu's linux-tools against the 6.12.112 benchmark kernel).
  The percentages include the query tool's own process start-up and result sort; `do_query`
  accounts for about 65% of all samples.
- `ftrace-sample.txt`: first 400 lines of a `function_graph` trace limited to `do_query` and its
  direct children (depth 2). Absolute durations are inflated by the tracer itself, which hooks
  every function inside the path walk; use it for the call structure, not for times.

Raw `perf.data` and the full trace: [`2026-10-11-azure-profile-q7.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-profile-q7.tgz) in the GitHub release
`bench-evidence`
(SHA-256 `6357fa1c96d344b2bebe29b4397fcc08e7d893541fb637d331ceb16cd2d3f805`). Originally attached to the pre-release `bench-2026-10-11-azure-profile-q7`; that tag remains. Interpretation: journal 0028.
