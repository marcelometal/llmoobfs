# 0182 — Resource cost of unprivileged QUERY2 domain rebuilds (issue 118)

Date: 2026-10-23. Status: **in progress; bounded probe prepared, VM result pending**.
Estimated effort: M (one to three days) for the investigation; started
2026-10-23, with labor time not yet measured. A mitigation, if selected,
needs its own effort estimate after the evidence is reviewed.

## Goal and starting evidence

Issue 118 asks whether a caller permitted to open a domain directory should
be able to make `QUERY2` scan the whole subtree, including after directory
renames. `layer/index.c` currently calls `domain_ensure()` before answering
such a query. It calls `scan_run()` with the mounter's credentials when a
domain is not fresh; `moobl_index_renamed()` marks the affected domain stale
after a directory rename. `index_budget` limits retained index memory, not
the CPU time or lower-filesystem reads needed for a build. This is a known
cost, not a newly reproduced fault.

The older release experiment (journal 0103) measured rebuilding a 7,500-file
domain on an earlier module and one host: 136–147 ms with pages cached, 2.07–
2.56 s after dropping the page cache. It did not isolate an unprivileged
post-rename `QUERY2` on the current module. ADR-0011 explicitly leaves fair
sharing and other resource limits outside its promises. Issue 116's memcg
accounting, merged by PR 129, charges memory but does not bound the scan.

This branch begins at `main` merge `40645e2`, separate from the documentation
PR for journal 0181. No production module behavior has been changed.

## Measurement decision and prepared probe

Measure before choosing a mitigation. A bounded guest step uses ext4 with one
domain containing 1,000 attributed files, a `QUERY2` issued after dropping
to uid/gid 1001, and the same query arguments for a cold first build, an
already-built query, and seven directory renames each followed by a rebuild
and another already-built query. The helper measures the ioctl's wall time,
process user/system CPU time and `/proc/self/io` `read_bytes` delta. The
existing `moobfs_domain_build` tracepoint and `GET_INFO` counters must show
exactly one first build and seven post-rename builds, with no build on the
warm queries. The run is limited to a dedicated VM and this small dataset;
it cannot be used as a general performance comparison.

Reusing journal 0103's results alone was insufficient because they are from
an older module and a release/rebuild scenario. Imposing a CPU or I/O limit
now would set new behavior before its tradeoffs and correctness effects are
measured. No policy or architectural decision has yet been made.

## Validation and limitations

The targeted native-KVM run and its exact output are pending. The static
helper compiled with `gcc -static -O2 -Wall -Wextra -Werror` against the
project's current UAPI header on the dedicated Linux host. `sh -n` passed
for `vm/suite.sh` and `vm/build-tests.sh`; `git diff --check` passed locally.
ShellCheck was unavailable in the local environment and has not been claimed
as passed. No claim of a reproduced severity, a measured new cost, or a fix
is made at this checkpoint.

## Next step

Compile and run the targeted guest probe on the dedicated Mac mini after
checking it is free. Review raw trace and CPU/I/O measurements, then decide
whether to extend the scales and lower filesystems or develop a bounded
mitigation with a negative regression test.
