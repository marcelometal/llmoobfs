# 0045 — Triage of the 30 open CodeQL alerts

Date: 2026-10-12. Branch: `fix/codeql-triage`, created from `origin/main` (`fccf38a`,
merge of PR #41). PR: https://github.com/studiomoob/llmoobfs/pull/42. Status: code
fixes implemented and tested; the dismissals on GitHub are left to the maintainer (writes to the code
scanning API denied in this session; see "Commands for the maintainer").

## Goal

Give a decision to each of the 30 open CodeQL alerts (`security-extended`, user-space C
in `tools/`, `common/` and `bench/`, introduced in journal 0039), before answering the
`static_analysis_fixed` criterion of OpenSSF Best Practices (project 15408). Nothing in `layer/`,
`kernel/`, the format, the UAPI or the benchmark's manifest and record formats changed.

Numbering: `origin/main` (last 0041), all remote branches (`m3/query-v2-q2b`
to `q2d` use 0036 to 0038; `m3/query-v2-q2d` also 0042 to 0044) and the local branches were checked. 0040
remains reserved for phase Q2e. The local branch `codex/kernel-index-reuse-research`, not
published, still has a duplicate `0039-kernel-index-reuse-research.md` and needs to be renumbered
to 0046 or later.

## Criterion

- (a) Fix when there is a real problem or when a small, clear change eliminates the pattern without
  changing behavior.
- (b) Dismiss when the alert describes the intended use: command-line tools that
  open the path the user themselves passed, and a benchmark harness that reads a manifest and
  query files chosen by the operator. None of these binaries is installed setuid;
  `load --uid N` only lowers privilege (`setgroups`/`setgid`/`setuid` to an ordinary user),
  never raises it. There is no privilege boundary between whoever supplies the data and whoever executes.

Dismissal reasons: `false positive` for `tools/` (the flow exists, but it is not a
vulnerability: opening the requested path is the tool's function); `used in tests` for
`bench/` (measurement harness, not installed nor used in production). `won't fix` was not used,
because it would suggest an accepted real defect.

## Decisions per alert

| Alert | Rule | Location (in `main`) | Decision | Justification |
| --- | --- | --- | --- | --- |
| 27 | `cpp/unbounded-write` | `bench/common.h:93` | Fixed | Confirmed false positive: `strlen(tok) >= BENCH_PATH_MAX` already rejected before the `strcpy` into `path[BENCH_PATH_MAX]`; now `memcpy` of the size checked against `sizeof out->path` |
| 28 | `cpp/unbounded-write` | `bench/common.h:113` | Fixed | Confirmed false positive: `strlen(tok) > 64` for `name[65]`; now `n >= sizeof a->name` and `memcpy`, same boundary |
| 29 | `cpp/unbounded-write` | `bench/common.h:117` | Fixed | Confirmed false positive: `strlen(colon) >= sizeof a->value` before the `strcpy`; now `memcpy` of the checked size |
| 30 | `cpp/world-writable-file-creation` | `bench/sqlclient.c:53` | Fixed | Record file created by `bench_create_out()` (mode 0644 before the umask) |
| 31 | `cpp/world-writable-file-creation` | `bench/load.c:536` | Fixed | Same (W3 records with `--uid`) |
| 32 | `cpp/world-writable-file-creation` | `bench/load.c:551` | Fixed | Same (W5 reader records) |
| 33 | `cpp/world-writable-file-creation` | `bench/load.c:445` | Fixed | Same (per-process records) |
| 34 | `cpp/world-writable-file-creation` | `bench/load.c:412` | Fixed | `/proc/sys/vm/drop_caches` is an existing file; opened with `open(O_WRONLY)` without `O_CREAT`, so nothing can be created |
| 35 | `cpp/world-writable-file-creation` | `bench/gen.c:81` | Fixed | Manifest created by `bench_create_out()` |
| 6 | `cpp/sql-injection` | `bench/sqlclient.c:72` | Dismiss (`used in tests`) | `sqlclient` is the W3 SQLite baseline: it executes, by definition, the queries of the file the operator passes (`bench/queries-w3.txt`); it is not a service and does not receive third-party input |
| 7 | `cpp/path-injection` | `bench/sqlclient.c:53` | Replaced by 38 (dismiss 38); see note | Operator's `--out` output prefix |
| 8 | `cpp/path-injection` | `bench/sqlclient.c:56` | Dismiss (`used in tests`) | Operator's `--sql` SQL file |
| 9 | `cpp/path-injection` | `tools/fsck.c:43` | Dismiss (`false positive`) | `fsck.moobfs IMAGE` reads the image the user themselves indicated, read-only, with their privileges |
| 10 | `cpp/path-injection` | `bench/load.c:536` | Replaced by 39 (dismiss 39); see note | Operator's `--out` prefix |
| 11 | `cpp/path-injection` | `bench/load.c:551` | Replaced by 40 (dismiss 40); see note | Operator's `--out` prefix |
| 12 | `cpp/path-injection` | `bench/load.c:445` | Replaced by 41 (dismiss 41); see note | Operator's `--out` prefix |
| 13 | `cpp/path-injection` | `bench/load.c:454` | Dismiss (`used in tests`) | Operator's `--root` (W8) |
| 14 | `cpp/path-injection` | `bench/load.c:460` | Dismiss (`used in tests`) | Operator's `--manifest` |
| 15 | `cpp/path-injection` | `bench/load.c:418` | Dismiss (`used in tests`) | Operator's `--target` (W6/W7) |
| 16 | `cpp/path-injection` | `bench/load.c:366` | Dismiss (`used in tests`) | Operator's `--queries` (W8) |
| 17 | `cpp/path-injection` | `bench/load.c:328` | Dismiss (`used in tests`) | Operator's `--root`/`--target` (query) |
| 18 | `cpp/path-injection` | `bench/load.c:291` | Dismiss (`used in tests`) | Operator's `--root` (W3) |
| 19 | `cpp/path-injection` | `bench/load.c:274` | Dismiss (`used in tests`) | `--target` + manifest path generated by `bench-gen` (W5) |
| 20 | `cpp/path-injection` | `bench/load.c:218` | Dismiss (`used in tests`) | Operator's `--queries` (W3) |
| 21 | `cpp/path-injection` | `bench/load.c:171` | Dismiss (`used in tests`) | `--target` + manifest path (W2) |
| 22 | `cpp/path-injection` | `bench/load.c:143` | Dismiss (`used in tests`) | `--target` + manifest path (W1, temporary file, already 0644) |
| 23 | `cpp/path-injection` | `bench/load.c:152` | Dismiss (`used in tests`) | Parent directory of the same path (W1, `fsync`) |
| 24 | `cpp/path-injection` | `tools/mkfs.c:52` | Dismiss (`false positive`) | `mkfs.moobfs` reads the `NAME=SOURCE` source files the user passed, read-only |
| 25 | `cpp/path-injection` | `bench/gen.c:81` | Replaced by 42 (dismiss 42); see note | Operator's `--out` |
| 26 | `cpp/path-injection` | `tools/mkfs.c:88` | Dismiss (`false positive`) | `mkfs.moobfs NEW_IMAGE`: creates the image the user asked for, with `O_EXCL` (does not overwrite) and mode 0600 |

Summary: 9 fixed (3 `unbounded-write`, 6 `world-writable-file-creation`); 21 with a dismissal
decision (20 `path-injection`, 1 `sql-injection`), of which 16 dismissed by the current
number and 5 (7, 10, 11, 12, 25) by the new alert that replaces them in the PR (38 to 42). No alert
revealed an exploitable defect.

Note on alerts 7, 10, 11, 12 and 25: the creation of the output files moved to
`bench_create_out()` in `bench/common.h`, so the `fopen` of these five flows no longer exists.
CodeQL reports the same flows as new alerts (38 to 42); see "CodeQL result on the PR".

## Changes

- `bench/common.h`: `bench_parse_line()` copies `path`, `name` and `value` with `memcpy` of the
  length already checked against the destination's `sizeof` (before, `strcpy` after the same
  check; for `name`, `> 64` became `>= sizeof a->name`, identical with `name[65]`). New
  `bench_create_out()`: `open(O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)` + `fdopen`.
  The top comment no longer says "no I/O" and cites the helper.
- `bench/load.c`, `bench/gen.c`, `bench/sqlclient.c`: the five output `fopen(..., "w"/"wb")`
  use `bench_create_out()`. In W6, `drop_caches` is opened with `open(O_WRONLY | O_CLOEXEC)` and
  `fdopen`, without `O_CREAT`.

Observable effect: only the mode of the output files when the umask does not remove write for
"others". With the usual umask 022 the result is still 0644. Manifest and record formats
unchanged (`bench-smoke` checks determinism and compares with the oracle).

## Tests executed

In a `ubuntu:24.04` container (aarch64, Docker on macOS), from a clean copy of the branch:

| Command | Result |
| --- | --- |
| `make test` | 9 tests, OK |
| `make test CC=clang BUILD=build/asan CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'` (the same step as CI) | 9 tests, OK |
| `make bench-smoke` | `BENCH_SMOKE_PASS` |
| `make bench-smoke CC=clang BUILD=build/asan-bench CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' LDFLAGS='-fsanitize=address,undefined'` | `BENCH_SMOKE_PASS`; one UBSan report in `tools/moobfs-index.c:194`, preexisting (see limitations) |
| `umask 000; bench-gen --seed 1 --inodes 5 --profile 6 --out /tmp/m.txt; stat -c %a` | 644 on this branch; 666 on `origin/main` (same container) |

The same `bench-smoke` run on `origin/main` printed the same output lines (including
`load: proc 0: 300 failed operations (recorded as errors)`, expected in the ordinary directory).
Not executed locally: `make vm-test` (the change does not touch what the VM exercises beyond
`bench-smoke`; the CI `Native Linux VM` job runs on the PR).

CI of PR #42 at `da1a760` (runs 38244739816 and 38244739750): `Lint`, `Tools and sanitizers`
(includes `make test` with ASan/UBSan and `make bench-smoke`) and `Analyze C (tools, common, bench)`
passed; `CodeQL` failed because of the new alerts 38 to 42 (next section); `Native Linux VM`
pending at the time of writing.

## CodeQL result on the PR

The PR analysis (run of the `CodeQL` workflow at `da1a760`) reported none of the alerts 27 to 35
on the changed lines, but opened five new `cpp/path-injection` alerts, 38 to 42, and therefore
the PR's `CodeQL` check went red ("5 new alerts including 5 high severity security
vulnerabilities"):

| New | Location (on the branch) | Replaces |
| --- | --- | --- |
| 38 | `bench/sqlclient.c:53` | 7 |
| 39 | `bench/load.c:540` | 10 |
| 40 | `bench/load.c:555` | 11 |
| 41 | `bench/load.c:449` | 12 |
| 42 | `bench/gen.c:81` | 25 |

These are the same flows (operator's `--out` prefix), now with the message "passed to
`bench_create_out(path)`, which calls `open`": the text of the line changed, so the alert's
fingerprint also changed. Any fix of the mode at these five points has this effect. CodeQL
only considers `umask()` called in the same function, and a global `umask` would also change the mode of the
directories created by W1. Alternative discarded: leave the five `fopen` as they were and
dismiss 30, 31, 32, 33 and 35, which would keep the check green and the files at 0666 minus the umask.
The fix stayed, because the request was the explicit mode. Decision for 38 to 42: dismiss as
`used in tests`, with the same justification as the alerts they replace. The old 7, 10, 11, 12
and 25 should close as `fixed` in the first analysis of `main` after the merge.

The PR analysis is restricted to the changed lines (diff-informed), so the absence of alerts
27 to 35 on the branch is only confirmed by the full analysis of `main` after the merge.

## Commands for the maintainer

Dismiss the 21 alerts in the table (except 7, 10, 11, 12 and 25, which will be replaced) and the five
new ones. Run 38 to 42 before the merge, so that the PR has no new open alerts (the PR
page and the check then reflect the dismissal; not confirmed in this session). The others can be
run at any time.

```sh
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/9 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: fsck.moobfs reads the image path its own user passed, read-only. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/24 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: mkfs.moobfs reads the NAME=SOURCE files its own user passed, read-only. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/26 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: mkfs.moobfs creates the image its own user named, O_EXCL and mode 0600. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/8 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --sql file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/13 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/14 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --manifest. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/15 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/16 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --queries file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/17 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root/--target. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/18 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/19 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/20 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --queries file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/21 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/22 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/23 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator parent of a --target manifest path. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/38 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 7). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/39 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 10). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/40 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 11). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/41 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 12). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/42 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out manifest path (replaces alert 25). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/6 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='sqlclient is the SQLite baseline of the benchmark and runs, by design, the SQL file its operator passes; not a service. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
```

Check after the merge and the analysis of `main`:

```sh
gh api 'repos/studiomoob/llmoobfs/code-scanning/alerts?state=open&tool_name=CodeQL&per_page=100' --jq 'length'
```

Expected: 0. Alerts 7, 10 to 12, 25 and 27 to 35 should appear with `state=fixed`.

## Limitations

- UBSan flags `qsort(NULL, 0, ...)` in `tools/moobfs-index.c:194` when no file
  matches; also reproduced on `origin/main`, outside the scope of this triage and not covered by the
  CI sanitizers step (which runs only `make test`). Trivial fix (`if (nhits)`), left
  as a next step.
- The dismissals hold as long as the binaries are not installed setuid nor exposed to
  third-party input; a change in that reopens the analysis.
- `make bench-smoke` under sanitizers was executed here, but is not in CI.

## Next step

Maintainer: dismiss 38 to 42 (the PR's `CodeQL` check depends on it), merge, dismiss
the others, check that no open alert remains and answer `static_analysis_fixed` citing this
journal. Then, fix the `qsort` in `moobfs-index` and consider
`bench-smoke` with sanitizers in CI.
