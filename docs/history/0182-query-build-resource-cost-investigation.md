# 0182 — Resource cost of unprivileged QUERY2 domain rebuilds (issue 118)

Date: 2026-10-23. Status: **in progress; bounded size probes passed; policy pending**.
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

Determine why cache dropping still yielded zero device-read sectors, then
measure lower-filesystem I/O in a controlled cold-cache setup. Use the
bounded CPU observations below to weigh an explicit resource policy against
the cost of refusing otherwise valid queries. Any selected behavior change
needs its own regression and negative run. No issue-closure decision has
been made.

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
The guest result is pending; no eviction result is claimed yet.
