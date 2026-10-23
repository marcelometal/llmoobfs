# 0182 — Resource cost of unprivileged QUERY2 domain rebuilds (issue 118)

Date: 2026-10-23. Status: **experimental implementation locally validated; not published; issue open**.
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
to uid/gid 1001, and the same query arguments for a first build after
requesting a cache drop, an
already-built query, and seven directory renames each followed by a rebuild
and another already-built query. The helper measures the ioctl's wall time,
process user/system CPU time and `/proc/self/io` `read_bytes` delta. A
later run also sampled the ext4 device's sectors-read counter. The
existing `moobfs_domain_build` tracepoint and `GET_INFO` counters must show
exactly one first build and seven post-rename builds, with no build on the
warm queries. The run is limited to a dedicated VM and this small dataset;
it cannot be used as a general performance comparison.

Reusing journal 0103's results alone was insufficient because they are from
an older module and a release/rebuild scenario. Imposing a CPU or I/O limit
now would set new behavior before its tradeoffs and correctness effects are
measured. No policy or architectural decision has yet been made.

## Validation and limitations

The static helper compiled with `gcc -static -O2 -Wall -Wextra -Werror`
against the project's current UAPI header on the dedicated Linux host.
`sh -n` passed for `vm/suite.sh` and `vm/build-tests.sh`; `git diff --check`
passed locally. ShellCheck was unavailable in the local environment and has
not been claimed as passed. The native-KVM results are recorded below.

## Next step

Use the bounded CPU, I/O and cross-user observations below to choose an
explicit resource policy with the maintainer. A behavior change needs its
own ADR, regression and negative run. If the current behavior is accepted
for experimental deployments, the public limitations must say so clearly.
No issue-closure decision has been made.

## First native-KVM attempt — 2026-10-23

The dedicated host had no QEMU process or layer mount at the preflight. The
branch at `20aca6c` was transferred as a verified complete Git bundle and
checked out separately on that host. `MOOB_ONLY=buildcost sh
build/native-kvm.sh` booted its Debian guest, passed the M1 image phase, then
stopped at `bc-first` before the query: the helper's read of `/proc/self/io`
returned `Permission denied` after it changed to uid/gid 1001. The harness
printed `MOOBFS_VM_FAIL: bc-first` and exited 1. This is a measurement-setup
failure, not a QUERY2 result or a module regression. No build-cost data from
this attempt is used.

The working hypothesis is Linux's dumpability change on `setuid`, which
restricts reading the task's `/proc/self/io`. The helper now calls
`prctl(PR_SET_DUMPABLE, 1)` after dropping privileges and before opening the
domain or measuring the ioctl. The program has no secret material, and this
change is confined to the disposable test guest. The repeat run at `497fb8f`
succeeded, which supports this explanation; the original `/proc` access
failure was not separately instrumented.

## Second native-KVM attempt — 2026-10-23

`MOOB_ONLY=buildcost sh build/native-kvm.sh` exited 0 on the dedicated
Debian 6.12.111 KVM host. The guest printed `MOOBFS_ONLY_PASS: steps
buildcost (not a complete run)`. For the 1,000-file ext4 domain, the first
unprivileged `QUERY2` took 14.181 ms wall time and 13.681 ms system CPU.
The seven queries after directory renames took 5.818–8.868 ms wall time and
5.826–8.882 ms system CPU, apart from one sample with 2.001 ms user and
5.849 ms system CPU. The paired already-built queries took 0.026–0.041 ms
wall time. The eight `moobfs_domain_build` trace records all returned error
0 and reported 1,131,120 bytes of index data each; their build durations
were 5.405–14.077 ms. `GET_INFO` and the trace count confirmed exactly
one first build and seven rebuilds, with no build on the warm queries.

`/proc/self/io` reported `read_bytes=0` for every ioctl, including the
first one after `drop_caches`. This is an observed counter value, **not**
evidence that the subtree walk performed no lower-filesystem I/O: it may
not attribute kernel work to this process, and no block-device counter was
measured. The first build after requesting a cache drop and subsequent
renames are not comparable as a controlled cold-versus-warm experiment. This run
does not establish a stable latency distribution, a production severity,
cross-user eviction, or a safe CPU/I/O budget. The module's behavior was
unchanged; no mitigation or regression-negative run is claimed.

Log: `~/moobfs-issue118-git-20261023/build/buildcost-0182-v2.log` and
guest console `build/vm/console.log` on the dedicated host. The first
failed attempt was preserved in `build/vm-attempt1`.

The zero process `read_bytes` prompted a follow-up probe: the helper also
samples the ext4 lower device's `/sys/block/vda/stat` sectors-read field
around each ioctl. This can include unrelated guest reads and is not exact
per-query attribution. The repeat at `80b29d0` exited 0 and again counted
one first build and seven post-rename rebuilds, but both process `read_bytes`
and device sectors-read deltas were zero for every ioctl. The first query
took 42.298 ms wall/40.537 ms system CPU; the seven post-rename queries
took 13.871–20.491 ms wall time. These are distinct samples, not a controlled
before/after comparison with the previous run, and no lower-I/O conclusion
can be drawn from zero counters. Log: `build/buildcost-0182-v3.log` on the
dedicated host; the prior guest output is preserved in `build/vm-attempt2`.

An opt-in `buildcostlarge` guest step runs the same sequence with 7,500
files. The full suite retains the 1,000-file step. Its targeted native-KVM
run at `d19fdb5` exited 0 with `MOOBFS_ONLY_PASS: steps buildcostlarge`.
The first query after the cache-drop request took 156.833 ms wall and
156.903 ms system CPU. The seven post-rename queries took 50.992–70.107 ms
wall and about 49.9–70.0 ms system CPU, while paired queries over the built
index took 0.020–0.104 ms wall. All eight build tracepoints returned error
0 and reported 8,482,620 bytes of index data; `GET_INFO` again showed one
first build, seven post-rename rebuilds and no warm-query rebuilds. Process
read bytes and device sectors read remained zero throughout. This supports
that a larger subtree raises CPU cost on this host, but it does not establish
a scaling law, cold-device I/O cost or behavior under concurrent users.
Log: `build/buildcost-0182-v4-large.log` on the dedicated host; the previous
guest output is preserved in `build/vm-attempt3`.

The phase label `first-cold` in these three run logs describes the requested
cache drop, not verified physical reads. The script now uses the more precise
label `first-after-drop-caches`; only this label changed after the large run.

## Cold-cache control being tested — 2026-10-23

The prior guest wrote and indexed a freshly planted tree while the lower
filesystem stayed mounted. A cache-drop request alone produced zero read
sectors; it did not prove the lower metadata was cold. The next bounded
attempt will sync, unmount the lower, request cache eviction, remount it and
only then mount the layer for the first query. After the query sequence and
cleanup, a read of a previously untouched raw-device sector checks that the
virtio block counter itself advances. This control reads but never writes
the disposable test device. The result is pending and is not yet evidence
of cold-cache I/O.

The targeted `MOOB_ONLY=buildcost` run at `ed5982a` exited 0 on the native
KVM host. After remounting the 1,000-file ext4 tree, the first unprivileged
query took 50.312 ms wall time and 43.234 ms system CPU, with 163,840
`read_bytes` and 320 device sectors read (also 163,840 bytes at 512 bytes
per sector). The seven post-rename queries took 10.284–19.556 ms wall time
and zero measured read bytes/sectors; the paired built-index queries also
read zero. Eight successful build tracepoints and `GET_INFO` again showed
one first build, seven rebuilds, and no paired-query rebuilds. A raw device
read after cleanup advanced the same sector counter by eight, validating
that it observed reads in this guest. This is a bounded, per-run observation,
not a distribution or proof that every lower-filesystem read is attributed
to the query task. Log: `build/buildcost-0182-v5-remount.log`; previous guest
artifacts are in `build/vm-attempt4` on the dedicated host.

The targeted `MOOB_ONLY=buildcostlarge` run of the same source also exited
0. After remounting the 7,500-file ext4 tree, the first unprivileged query
took 201.097 ms wall time and 182.798 ms system CPU, with 1,990,656
`read_bytes` and 3,888 device sectors read (the same 1,990,656 bytes at
512 bytes per sector). The seven post-rename rebuild queries took
54.088–92.168 ms wall time with zero additional measured read bytes or
sectors; paired built-index queries took 0.020–0.068 ms, also with zero
reads. Eight successful build tracepoints and `GET_INFO` confirmed exactly
one first build and seven rebuilds. The raw-device control advanced by
eight sectors. Log: `build/buildcost-0182-v6-large-remount.log`; previous
guest artifacts are in `build/vm-attempt5` on the dedicated host.

These two runs demonstrate that remounting after the cache-drop request
exposed read I/O for this dataset, and that the larger tree incurred more
measured work on this host. They do not provide a latency distribution or
identify the cost on slower devices. The cross-user budget-eviction part of
issue 118 has not yet been directly exercised; that is the next bounded
probe.

An opt-in cross-user probe is being prepared. It gives uid 1001 and uid
1002 separate private ext4 domains of 1,000 attributed files, measures one
domain's retained size, then mounts with a budget that fits one but not
both. Queries under 1001, 1002 and 1001 in that order must show three builds
and budget releases in `GET_INFO` and tracepoints. This distinguishes
cross-user eviction from the already demonstrated same-domain rename cost.
The targeted `MOOB_ONLY=buildcostevict` KVM run at `cb019c7` exited 0.
The measured first domain used 1,131,120 index bytes; the test chose a
1,762,216-byte mount budget, enough for one of the private domains but not
both. Three successful unprivileged queries ran as uid 1001 on `a`, uid
1002 on `b`, then uid 1001 on `a` again. `GET_INFO` counted three builds
and at least two releases; the tracepoint recorded `by=budget` releases of
the 1,131,120-byte `a` and `b` domains in turn. The three queries took
19.334, 29.243 and 23.703 ms wall time respectively in this one warm-cache
run. The module did not change. This directly demonstrates cross-user
eviction and repeated CPU cost under a shared mount budget, not its
frequency or severity in production. Log:
`build/buildcost-0182-v7-cross-user.log` on the dedicated host.

## Alternatives and decision gate

The existing on-demand design deliberately performs a full synchronous
domain scan (ADR-0012), and ADR-0011 does not promise fair budget shares or
other resource limits. The evidence above makes the cost and cross-user
effect concrete; it does not by itself choose a policy.

- Retain the current query behavior and explicitly limit deployment to
  trusted or single-user mounts until S9. This avoids denying a valid query
  but leaves CPU, I/O and cross-user eviction under user control. Documenting
  the risk would be acceptance, not a fix.
- Cap work per build and rate-limit repeated builds per caller and mount.
  A request that crosses a cap must return an error without partial results;
  otherwise callers can simply repeat the same scan. The cap, error and
  fairness rules would be new observable query behavior, requiring an ADR,
  regression and negative tests, and a benchmark of normal workloads.
- Rework directory-rename invalidation to update index paths instead of
  rescanning. This could reduce the measured post-rename work but leaves the
  first-build and budget-eviction problems. It is not a complete response
  to issue 118 and carries concurrency and correctness risk.

The investigation recommends keeping issue 118 open. No mitigation,
regression-negative test or issue closure has been performed. Controlled
latency distributions, slower lower devices and concurrent-user load remain
unmeasured.

## Decision and design round — 2026-10-23

After reviewing the measured CPU/I/O cost and cross-user eviction, the
maintainer chose to design limits on scan work and reconstruction frequency,
accepting that queries above the eventual limit may return an error. This
selects the resource-limit alternative above; it does not authorize a claim
that a numerical threshold is already validated. ADR-0019 records the
proposed admission and cumulative-work mechanism. The design counts all
attempts and retries, fails explicitly without a successful partial result and
distinguishes a work-operation cap from a strict CPU-time or physical-I/O
cap. The latter is not established by the current observations.

The next round is to select and validate concrete thresholds and the errno
contract, then implement the guard and test it negatively and in the full
VM/KASAN suite. No production source was changed in this design round.

Dated correction, 2026-10-23: ADR-0019 initially said no QUERY2_BATCH output
would be copied on a build-limit error. Code inspection showed that the batch
path can copy earlier successful subquery result buffers before a later
subquery fails. The existing client contract requires discarding every output
on any batch error. ADR-0019 now states this actual contract; the proposed
limit must preserve and test it, not claim that zero bytes reach userspace.
The public-limit text was reconciled in both READMEs, SECURITY, the security
roadmap, the when-not-to-use guide and the site guide. `make test` passed 60
attribute checks and nine image tests; the retired-claim checker, site-link
checker, FAQ synchronizer and `git diff --check` passed. No module VM or
KASAN run was made for this documentation-only round.

## Initial limit implementation and calibration — 2026-10-23

Three additional targeted native-KVM runs at the same source (`cb019c7`)
repeated each ext4 size after remount. The 1,000-file first builds took
45.932–49.011 ms wall time and read 159,744–167,936 bytes; the 7,500-file
first builds took 196.926–210.976 ms and read 1,986,560–2,002,944 bytes.
All six runs passed their eight-build and no-extra-warm-build assertions.
Logs are `build/calib-0182-buildcost-{1,2,3}.log` and
`build/calib-0182-buildcostlarge-{1,2,3}.log` on the dedicated host. This is
one ext4 VM on one host, not an empirical distribution across supported
lower filesystems or slower disks.

The first implementation uses 32,768 cumulative scan work units per domain
build (enumerated names, directory visits and `index_load` attempts), a
cooperatively checked ten-second deadline, and a fixed 60-second admission
window of 16 attempts per unprivileged UID and 64 per mount. Administrators
are exempt from frequency admission but not from the per-build work and time
limits; this keeps the existing privileged mount-wide QUERY/stress semantics.
The initial numbers are experimental guardrails: the observed 7,500-file
case needs about 15,000 work units and passes, while a larger domain may be
refused. No claim of a strict CPU-time or physical-I/O-byte cap follows.
Admission uses `EDQUOT`, work exhaustion `E2BIG`, and timeout `ETIMEDOUT`.
The source currently has no mount option to raise these limits; this is an
intentional compatibility change to test before publishing.

The first patched `MOOB_ONLY=buildcostlarge` targeted KVM run at `cac4447`
passed: first query 214.655 ms and 1,990,656 read bytes, eight expected
builds, no extra build on warm queries. This confirms only that fixture, not
the over-limit or concurrency behavior. Log:
`build/buildlimit-0182-smoke.log` on the dedicated host.

The first opt-in `buildlimit` guest attempt at `5650ed1` stopped during
fixture creation, before a MoobFS query: ext4 reported `No space left on
device` for file 16,367 of 17,000. The prepared ext4 device had too few
inodes for this fixture. The harness now reformats that disposable test
device with 40,000 inodes before planting; the rerun is pending. The failed
attempt is not evidence of a limit pass or module failure. Log:
`build/buildlimit-0182-v1.log` on the dedicated host.

The `buildlimit` rerun at `b0b4f7d` exited 0 in native KVM. With the
17,000-file attributed fixture, the first `QUERY2` returned `E2BIG` and did
not modify the caller's result buffer, count or cursor. A second mount
confirmed 16 builds for uid 1001, `EDQUOT` on its next stale build, a
successful build for uid 1002 and a successful fresh-domain query by uid
1001 without admission. A fresh mount then allowed 64 builds across uids
1001–1004, returned `EDQUOT` for uid 1005, and allowed its pending build
after a real 61-second wait. `GET_INFO` counts and the final
`BUILD_LIMIT_PASS` and `MOOBFS_ONLY_PASS` markers matched. Log:
`build/buildlimit-0182-v2.log` on the dedicated host.

The attributed fixture fit after inode reprovisioning, but its external
xattr blocks used more of the 64 MiB device than the work-limit behavior
requires. A follow-up fixture creates 17,000 empty files instead: each
still consumes an enumerated-name and an `index_load` attempt, enough to
exceed 32,768 units, while using less disk. The targeted KVM run at
`fd03229` again exited 0 with `BUILD_LIMIT_PASS` and `MOOBFS_ONLY_PASS`;
log `build/buildlimit-0182-v3-empty.log`. This is the regression kept in
the branch.

The negative branch `neg/issue118-buildlimits-0182` at `e42a2ba`
contains the same test but the pre-limit module from `ea86eac`. Its
targeted native-KVM run exited nonzero at the intended first assertion:
`query-build-limit` observed errno 0 and count 0 instead of `E2BIG`, then
`MOOBFS_VM_FAIL: bl-work-errno-or-output`. Log:
`~/moobfs-issue118-neg-20261023/build/buildlimit-0182-negative.log` on the
dedicated host. This verifies the work-limit regression detects absence of
the guard. It does not separately prove the UID and mount-frequency
assertions fail without the guard; the positive run exercises them.

The complete native KVM suite on the patched branch has started; its result
is pending. KASAN/lockdep, btrfs/XFS-specific limit fixtures, a controlled
performance comparison, publication and issue closure have not yet run.

The first complete native-KVM suite at `fd03229` did **not** pass. It
reached and passed the ext4 QUERY2 steps, then stopped at
`MOOBFS_VM_FAIL: race-query-ext4`: the five-second legacy mount-wide
`QUERY` race (with `domain_depth=0`) received `EDQUOT` after the new
admission count was exhausted. Log: `build/buildlimit-0182-full.log` on the
dedicated host. This is an integration incompatibility found by the full
suite, not a kernel crash or a partial answer. The adopted scope of issue
118 is unprivileged `QUERY2` on a domain. Commit `5e3ea9a` therefore charges
frequency only when `domain != 0`; the per-build work and cooperative-time
limits still apply to legacy whole-mount scans. The unprivileged legacy
`QUERY` when domains are disabled remains outside the frequency guard and
is a documented residual availability risk. The complete suite must be
repeated on this new head; no pass is claimed yet.

## Full-suite and deadline follow-up — 2026-10-23

The second complete native-KVM run at `5e3ea9a` exited 0 on the dedicated
Debian 6.12.111 host. The guest emitted `MOOBFS_VM_PASS`,
`MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS` after the ext4, btrfs and
XFS lower-layer checks, race/release cases, SQLite and memory-client
acceptance, and malformed-image tests. The log is
`~/moobfs-issue118-git-20261023/build/buildlimit-0182-full-v2.log`, with the
guest console preserved in `build/vm-full-v2/console.log`. This run used the
frequency exception for legacy mount-wide `QUERY`; it does not exercise the
later opt-in deadline fixture or the btrfs/XFS over-limit fixture.

The targeted `MOOB_ONLY=buildtime` native-KVM run at `76a7602` exited 0. A
test-only 200 ms per-file scan delay made the 60-file first build exceed the
cooperative ten-second deadline. `QUERY2` returned `ETIMEDOUT` without
changing its result buffer, count or cursor; after clearing the delay, a
second query succeeded and `GET_INFO` recorded one completed build. The
guest emitted `BUILD_TIME_PASS` and `MOOBFS_ONLY_PASS`. Log:
`build/buildtime-0182.log`, console: `build/vm-buildtime/console.log` on the
dedicated host. This is a controlled deadline test, not a bound on one
blocking lower-filesystem operation.

Commit `70f57fb` extends the opt-in work-limit fixture to btrfs and XFS;
its targeted run is pending. The full KASAN/UBSAN/lockdep suite and a
before/after latency comparison are also pending. The cumulative allowance
across moved-directory rescans and retries is apparent in the single
`scan_job` shared by `domain_ensure()` but still lacks a forced regression
that distinguishes it from a reset on retry. Publication and issue closure
remain blocked on the verification gate of ADR-0019.

## KASAN integration failure — 2026-10-23

The first complete local KASAN/UBSAN/lockdep run at `76a7602` did **not**
pass. `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS` were emitted, but the
subsequent `permission-gaps faults` sweep stopped at
`MOOBFS_VM_FAIL: fi-unprivileged-permissions`. Its first three warm-domain
shapes passed. The guest then printed `PERMISSION_GAPS_FAIL: QUERY2
(errno=122)`, which is `EDQUOT`, followed by a failed fault-result read.
The sweep opens many distinct fresh domains as the same unprivileged UID;
the new 16-attempt UID admission window is reached before the sweep can
finish. This is a test-fixture incompatibility with the chosen observable
limit, not a KASAN or lockdep diagnostic. It is not a KASAN pass.

The fault trials are independent. The harness now remounts the layer before
each group of eight fresh-domain trials, leaving at most two admitted builds
per trial (the injected call and its retry) within one new admission window.
The next round must run the targeted fault step and then repeat full KASAN on
this harness change. Log: `build/kasan-0182.log`; guest console:
`build/vng/console.log` on the dedicated host.

The isolated `MOOB_ONLY=faults` KASAN/UBSAN/lockdep run at `d327a40`
subsequently exited 0 with `MOOBFS_KASAN_ONLY_PASS`. All six
unprivileged fault shapes passed, with 204 actual injected faults in 210
calls, and no kernel diagnostic. This validates the fixture repair only;
the repeated full KASAN run is in progress.

Commit `7028c6e` adds a separate opt-in moved-directory regression. It
uses function tracing to rename a directory after `index_load` begins,
then requires `E2BIG` when the original and side scans together cross the
single work allowance; each scan alone is below the cap. Its helper compiled
with the remote Linux compiler, `make checkpatch`, ShellCheck and
`git diff --check` passed. The VM result is pending, so cumulative accounting
has not yet been validated dynamically by this test.

## Complete KASAN repeat — 2026-10-23

The repeated complete KASAN/UBSAN/lockdep run at `d327a40` exited 0 on the
dedicated KVM host. It emitted `MOOBFS_ACCEPTANCE_PASS`,
`MOOBFS_LAYER_PASS` for ext4, btrfs and XFS, and `MOOBFS_KASAN_PASS` with no
kernel diagnostic. The host wrapper reported `kasan-local: pass`. Log:
`~/moobfs-issue118-git-20261023/build/kasan-0182-v2.log`; guest console:
`build/vng/console.log`. This run included the isolated fault-sweep repair,
but predates the optional moved-directory test, which is outside the full
KASAN suite. It does not resolve the residual legacy whole-mount QUERY
frequency risk or prove strict CPU/I/O caps.

## Cross-filesystem work cap and moved scan — 2026-10-23

The opt-in `MOOB_ONLY="buildlimit buildmoved"` native-KVM run at `7028c6e`
exited 0. The `buildlimit` fixture denied the 17,000-empty-file query with
`E2BIG` on ext4, btrfs and XFS without changing the caller's result buffer,
count or cursor. Its frequency checks again exercised separate UIDs, the
mount-wide limit, fresh-domain bypass and recovery after a real 61-second
wait. The guest printed `BUILD_LIMIT_PASS` and `MOOBFS_ONLY_PASS`.

In the same run, PID-filtered function tracing observed five `index_load`
calls before a same-domain directory rename. The query returned `E2BIG`
without published output when its first scan and moved-directory rescan
together exceeded the work allowance; the guest printed
`QUERY_BUILD_MOVED_CHILD_PASS` and `QUERY_BUILD_MOVED_PASS`. Each side alone
is below the allowance. This tests one forced moved-rescan path; it does not
exhaustively cover every budget-release or rename retry schedule. Log:
`~/moobfs-issue118-git-20261023/build/buildlimits-0182-crossfs-moved.log`;
guest console: `build/vm-crossfs-moved/console.log` on the dedicated host.

The before/after cold-query latency comparison is still pending. The module
and test branch remains local; no issue update, push or PR was made.

## Paired performance pilot — 2026-10-23

Eight sequential before/after native-KVM pairs ran `buildcostlarge` on a
7,500-file ext4 domain, rebuilding the disposable guest and remounting the
lower filesystem before the first query on each run. The host governor was
held at `performance` with turbo off, then restored to `schedutil` and its
original turbo setting. Every run emitted `MOOBFS_ONLY_PASS`, returned one
result, and reported process read bytes equal to the sampled block-sector
delta. `bench/compare.py` found a before median of 162.113 ms and after
median of 161.813 ms for that first query, a -0.2% relative difference with
95% bootstrap interval -2.1% to +1.8% and Mann–Whitney p=0.958. It reported
no difference detected and warned that eight repetitions are fewer than the
30 required by the project benchmark plan. The pilot always ran before then
after, so execution-order drift is a further limitation. Raw logs and guest
consoles are preserved under `build/compare-0182/` and
`build/vm-compare-{before,after}-[1-8]/` in the two dedicated-host checkouts;
converted records and CSVs are in the positive checkout's
`build/compare-0182-report/`. No general performance claim follows from this
pilot. A separate 30-pair series with alternating order is in progress.

## Thirty-pair controlled comparison — 2026-10-23

The separate formal series completed 30 native-KVM before/after pairs, with
the order alternated on odd/even pairs. `before` was the pre-limit module at
`e42a2ba` with the same measurement helper; `after` was the limited module at
`7028c6e`. Each run rebuilt the disposable guest, planted the 7,500-file
ext4 fixture, remounted the lower filesystem and measured the first
unprivileged `QUERY2` that built the domain. Host CPU governors were fixed at
`performance` and turbo disabled during the series. All 60 runs exited 0,
emitted `MOOBFS_ONLY_PASS`, and returned exactly one result. The script
restored `schedutil` on all eight CPUs and the prior turbo setting; no QEMU
process or saved frequency-state file remained after completion.

For the first query, before/after medians were **163.214/164.638 ms** wall
time, respectively. Across repetitions, p5–p95 was 157.868–168.116 ms
before and 161.651–168.455 ms after. Median process CPU time was
153.989/155.469 ms; median process `read_bytes` was **1,994,752 bytes in
both**. `bench/compare.py` reported a **+0.9%** relative difference in wall
medians (after minus before), 95% bootstrap interval **+0.3% to +1.8%**,
Mann–Whitney p=0.00486, `n=30/30`, verdict `differs`. Because each
repetition has only one first-cold query, the tool's per-repetition p50,
p95 and p99 entries are identical; only the first-query comparison is
interpreted. Descriptively, 22 of 30 paired after runs were slower, and the
median paired relative increase was 1.402%. The effect is small but was
detectable in this controlled shape; unlike the pilot, it must not be
described as zero overhead. The likely contributors are the new scan checks
and admission accounting, but this experiment does not isolate them.

The initial parser rejected one otherwise valid run because it required
process `read_bytes` to equal the whole guest-device sector delta. In the
25th before run, the latter was 4,096 bytes greater. The device counter can
include unrelated guest reads, so equality was an invalid control. The
parser was corrected to require a valid result count and retain both I/O
counters and their difference. Exactly one of 60 runs had a nonzero
device-minus-process delta (4,096 bytes); no sample was discarded. This
does not establish an exact physical-I/O attribution or a hard byte cap.

Reproducible command on the dedicated host: `sh ~/issue118-compare30.sh`,
then `python3 ~/issue118-parse30.py`. The scripts alternate the checkout
order and run `MOOB_ONLY=buildcostlarge sh build/native-kvm.sh` once per
side and pair; they call `bench/cpu-freq.sh` to fix and restore the host
state. Scripts plus `raw.csv`, `compare.csv` and `ecdf.csv` were copied to
`MoobFS-local/issue118-bench-20261023/` outside Git. The 60 guest consoles
and host logs remain in each Mac mini checkout under
`build/vm-compare30-{before,after}-[1-30]/` and
`build/compare30-0182/`; the positive checkout also has
`build/compare30-0182-report/` with the converted records and CSVs.
The experiment covers one host, ext4 and this cold-query shape; it does not
show behavior on slow lower devices, other workloads or concurrent callers.

The local verification gate of ADR-0019 now has a negative control, normal
and over-limit builds, timeout and frequency recovery, a forced cumulative
moved rescan, the full native-KVM and KASAN suites, and the 30-pair latency
comparison. The amount of CPU time and lower I/O saved by cutting off the
17,000-file query, compared with allowing it to finish, was not measured.
Before publication, the maintainer still needs to review the
availability tradeoff for legitimate domains above the fixed cap and the
measured +0.9% overhead. The lack of strict CPU/I/O caps, the legacy
whole-mount frequency exception and unmeasured slower devices remain
explicit limitations. Issue 118 stays open; there has been no push, PR,
public issue update or merge for this branch.
