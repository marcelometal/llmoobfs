# 0185 — Query-build policy at larger domains and delayed lower I/O

Date: 2026-10-24. Status: bounded measurement complete; policy decision open.
Work started and ended: 2026-10-24. Labor time: not tracked separately.

## Goal and decision

After PR 135 merged the experimental guard, the maintainer chose to keep
issue 118 open and measure larger domains and slower lower storage before
changing the numerical policy or closing the issue. The motivation is the
legitimate 17,000-file domain denied just above the 32,768-work-unit cap:
the controlled comparison in journal 0182 saved only 3.370 ms of paired
median process CPU at that threshold, while a 60,000-page bulk client replay
covered ten domains of about 6,000 pages on one host. Neither result chooses
a general threshold or proves a strict CPU/I/O cap.

## Planned bounded experiment

Use a dedicated VM on the Mac mini's Debian 6.12.111 KVM host. Prepare a
fresh, uniquely named ext4 disk image, plant a specified count of empty
files with exclusive creation, and issue one unprivileged cold `QUERY2`.
Repeat on the same lower image through the kernel's `dm-delay` target, with
a documented read delay; unmount and request cache eviction before each
condition. Record the ioctl verdict, wall and process CPU time, task read
bytes and underlying block-device read sectors. The helper checks that a
denied call leaves its output untouched. Keep the disk, VM logs and host
configuration outside Git; version only the runner and factual summary.

This is a synthetic lower-I/O sensitivity probe, not a claim that a
particular HDD, network filesystem or cloud device was tested. Device
read-sector counts include other guest activity and are not an exact
per-query accounting source. A configured delay does not convert the
cooperative ten-second deadline into a hard CPU or I/O bound. Run a small
pilot before larger cases and refuse existing output directories or disk
images rather than overwrite them.

## Results and next step

### Harness pilot — 2026-10-24

The dedicated host was reachable, its Debian 6.12.111 kernel had
`CONFIG_DM_DELAY=m` and `dm-delay.ko.xz`, and no QEMU process was active.
The first SSH preflight omitted the `debian` login name and failed before
running a remote command; the corrected destination connected. A local Mac
syntax-only C check could not find Linux's `sys/prctl.h`; this is not a C
test verdict, and the helper was instead compiled statically on the Linux
host. `sh -n`, `git diff --check`, `make checkpatch`, the retired-claims
check and the site check passed before the test-harness commit `cb1190d`.
The branch was transferred with a bundle verified at both ends; the clean
host checkout switched to the same commit without a public push.

The first opt-in command was
`sh vm/issue118-policy-host.sh pilot1000 1000 5`. It created a new 128 MiB
ext4 image and completed with `MOOBFS_POLICY_PASS`. The unprivileged cold
query returned a full empty answer in both conditions. Direct virtio disk:
23.202 ms wall, 21.470 ms process user+system CPU, 184,320 task read bytes
and 304 underlying read sectors. Through `dm-delay` configured for 5 ms
read delay and zero write delay: 95.397 ms wall, 24.792 ms process CPU,
184,320 task read bytes and 304 sectors. The guest printed the six-argument
device-mapper table with the configured delay. These are single pilot
observations, not a paired statistical effect or a real slow device result.
The image and console remain under `build/issue118-policy/pilot1000/` on
the dedicated host; that output name cannot be reused by the runner.

### Empty-file domain sweep — 2026-10-24

The same committed runner then executed `files12000 12000 5`,
`files17000 17000 5`, `files24000 24000 5` and `files30000 30000 5`,
each as `sh vm/issue118-policy-host.sh RUN_ID FILES READ_DELAY_MS` in a new
KVM guest and image. All four exited zero with `MOOBFS_POLICY_PASS`; the
helper validated untouched query output on each denial. The host checkout
remained clean and no QEMU process remained after the runs.

| Files | Direct verdict/wall ms | Delayed verdict/wall ms | Task read bytes, direct/delayed | Underlying read sectors, direct/delayed |
| ---: | --- | --- | ---: | ---: |
| 12,000 | full / 112.096 | full / 975.682 | 3,244,032 / 3,244,032 | 6,280 / 6,280 |
| 17,000 | `E2BIG` / 172.576 | `E2BIG` / 1,415.784 | 4,669,440 / 4,669,440 | 9,064 / 9,080 |
| 24,000 | `E2BIG` / 253.347 | `E2BIG` / 1,467.573 | 6,410,240 / 6,410,240 | 12,464 / 12,464 |
| 30,000 | `E2BIG` / 162.084 | `E2BIG` / 1,575.927 | 7,958,528 / 7,958,528 | 15,488 / 15,488 |

These are one direct and one delayed query per size, not a randomized
performance study. The 30,000-file direct time being shorter than the
24,000-file direct time cautions against a monotonic latency claim.
Read bytes and sectors rose across the planted sizes even though the work
allowance denied all three larger queries; they do not establish which
lower operation caused the difference or a strict I/O bound. The injected
5 ms read delay produced a material wall-time difference in this guest,
but did not reach the cooperative ten-second deadline. Raw images,
environment files and console logs remain in distinct
`build/issue118-policy/files*/` directories on the host.

The next round uses the six-attribute fixture from the earlier 6,000-file
client-shape control at larger per-domain sizes. This distinguishes the
empty-file sensitivity sweep from a closer, still synthetic client shape.
No policy change or issue closure follows from these observations.

### First six-attribute attempt — setup failure, 2026-10-24

`sh vm/issue118-policy-host.sh six12000 12000 5 six` created a new image
and planted its files, but the measurement helper exited before emitting a
result: `QUERY2` succeeded with `count=1` and `truncated=1`, while that
helper had assumed an empty answer because its `kind EXISTS` term matched
none of the earlier empty-file fixtures. This is an oracle/setup mismatch,
not a module regression or a timed policy result. The runner exited 1 and
kept `build/issue118-policy/six12000/console.log` and its image; no
success marker was printed. The correction is to query an absent attribute
name while keeping the six typed attributes on each file, so the full
index build still occurs but the established empty-answer oracle applies.
The retry will use a distinct output name and will not reuse the failed
image.

### Six-attribute scale and delay control — 2026-10-24

The absent-term correction was committed in `d96b475`, transferred by a
second verified bundle and fast-forwarded into the clean dedicated-host
checkout. `six12000retry` and `six17000` each exited zero with a
`MOOBFS_POLICY_PASS` marker using a 5 ms read delay; `six17000d20` did so
with a 20 ms read delay. The query term was absent, but the six typed
attributes on every file were still loaded into the index. `count=0` on
successful queries and the sentinel `count=777` on denials were checked by
the helper. These are separate cold images, one query per condition.

| Files / delay | Direct verdict, wall ms | Delayed verdict, wall ms | Direct/delayed task reads | Direct/delayed read sectors |
| --- | --- | --- | ---: | ---: |
| 12,000 / 5 ms | full, 220.927 | full, 1,326.285 | 3,297,280 / 3,297,280 B | 6,376 / 6,376 |
| 17,000 / 5 ms | `E2BIG`, 345.379 | `E2BIG`, 2,061.553 | 4,780,032 / 4,780,032 B | 9,272 / 9,272 |
| 17,000 / 20 ms | `E2BIG`, 424.184 | `E2BIG`, 3,783.395 | 4,730,880 / 4,730,880 B | 9,192 / 9,192 |

The 20 ms control did not reach the ten-second cooperative deadline; it
demonstrates an expensive refusal under synthetic delayed reads, not a
guaranteed maximum for slower storage. The different images also explain
why their read counters should not be treated as a paired delta. Raw logs
and images remain under the distinct run IDs on the host.

The next comparison uses the prior production module at `f283f13`, which
completed the 17,000-file query before the guard (journal 0182). The source
diff from that commit to the current branch under `layer/` is confined to
the guard in `index.c` and its accounting fields in `moobl.h`. The runner
will compile that commit from a Git archive inside a new run directory,
using the same current query helper and the same guest procedure, without
opening or altering a second worktree. This tests the time saved by denial
on delayed lower I/O; it does not by itself choose the acceptable limit.

### Initial pre-limit comparison — 2026-10-24

The pre-limit module compiled from archived source at `f283f13` and ran
through the same guest helper on new six-attribute, 17,000-file images.
`six17000pre5` and `six17000pre20` both emitted `MOOBFS_POLICY_PASS` and
completed full answers under direct and delayed reads. At 5 ms read delay,
the pre-limit delayed query took 1,970.119 ms wall and 824.830 ms process
CPU with 4,743,168 task read bytes and 9,200 underlying read sectors.
At 20 ms, it took 3,920.441 ms wall and 884.565 ms process CPU with
4,755,456 task read bytes and 9,240 sectors. The guarded, denied queries
in the separate images above took 2,061.553 ms at 5 ms and 3,783.395 ms
at 20 ms. The signs of those single-run wall differences disagree, so
neither is treated as an effect of the guard. All four runs used distinct
filesystem images and uncontrolled host CPU frequency.

To remove filesystem-layout variation, the next bounded comparison copies
one unmounted 17,000-file image from `six17000` into each new run. The
runner records the disk hash before the guest uses it, validates the seed's
file count and shape, and refuses reused output names. A new opt-in pairs
script alternates module order and runs the two VMs sequentially. Results
will be analyzed as pairs; no frequency adjustment is planned. The
synthetic delay and one host remain limitations.

### Matched-image, alternating-order comparison — 2026-10-24

The pilot `pilotclone` ran each module once on a copy of the same unmounted
17,000-file, six-attribute image with a 20 ms read delay. Both VMs emitted
`MOOBFS_POLICY_PASS`; the guarded delayed query returned `E2BIG` in
4,056.109 ms and the pre-limit query returned its full answer in
4,071.337 ms. This pilot established the cloned-image path, not a stable
effect estimate.

`sh vm/issue118-policy-pairs.sh pair20a 10 17000 20 six six17000`
then completed ten sequential pairs, alternating which module ran first.
All twenty VMs emitted `MOOBFS_POLICY_PASS`: the current guard denied the
query and preserved the sentinel output; the pre-limit module completed
the empty answer. The analyzer validated each run's fixture settings,
verdict, result markers, alternating console timestamps and identical
pre-run disk SHA-256 across all twenty copies. These pairs used one seed
image; no other QEMU ran on the host. The metrics below are medians across
the ten paired current-minus-pre-limit differences; the interval is a
percentile bootstrap of those ten differences (20,000 draws, fixed seed).

| Condition / metric | Current median | Pre-limit median | Paired difference median | Bootstrap 95% interval | Current lower in pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| 20 ms delayed, wall | 4,050.439 ms | 4,072.482 ms | −17.945 ms | −32.397 to −10.659 ms | 9/10 |
| 20 ms delayed, process CPU | 895.774 ms | 882.974 ms | +13.262 ms | +5.134 to +35.152 ms | 1/10 |
| 20 ms delayed, task reads | 4,780,032 B | 4,788,224 B | −8,192 B | −8,192 to −8,192 B | 10/10 |
| 20 ms delayed, device read sectors | 9,288 | 9,304 | −16 | −32 to −16 | 9/10 |
| Direct, wall | 396.771 ms | 385.101 ms | +13.015 ms | −17.976 to +42.281 ms | 4/10 |
| Direct, process CPU | 358.425 ms | 349.548 ms | +10.150 ms | −15.829 to +34.095 ms | 4/10 |
| Direct, task reads | 4,780,032 B | 4,788,224 B | −8,192 B | −8,192 to −8,192 B | 10/10 |

The observed delayed wall saving at the denial boundary is about 0.44%
of the pre-limit median, while the guarded query consumed more process CPU
in nine pairs. The identical 8 KiB task-read difference shows the guard
returns close to the end of this build. These are measurements of this
fixture, host and synthetic delay, not a general performance guarantee.
The bootstrap interval describes resampling uncertainty within these ten
serial pairs; it does not account for other hosts, physical media, client
shapes, host frequency variation or longer term drift. Device sectors
include guest activity beyond the query. No query reached the cooperative
ten-second timeout, so its behavior on a slower device remains unmeasured.

The raw twenty console logs and environment records are retained outside
Git on the dedicated host at
`build/issue118-policy/pair20a-logs.tar.gz` and locally at
`MoobFS-local/issue118-policy-20261024/pair20a-logs.tar.gz` (SHA-256
`30d4eb4f7a774a18bafa51e3cf2219b8ef7a6854721326ed9d662a823598eb7f`).
The local JSON analysis is `pair20a-analysis.json` in the same folder
(SHA-256 `b9b53d3c79603aeba3a06a0896c0ad03d234a0be9be364de3aabb1e331b371df`).
Reproduce the summary with
`python3 bench/issue118-policy-pairs.py ARCHIVE pair20a 10 17000 20 six six17000`.
The twenty disk images remain under their distinct run IDs on the host.

The fixed 32,768-work-unit limit should stay described as experimental:
this measurement confirms a legitimate 17,000-file domain is refused after
nearly all of its measured reads, with little wall time saved on this
synthetically delayed lower device. There is insufficient evidence here to
choose a new universal limit or assert the guard bounds I/O. Keep issue
118 open for the compatibility/policy decision; a configurable limit or
domain partitioning can be evaluated with real client shapes and storage
classes. No production guard code, published limit or issue state changed
in this investigation.

The archived-pair analyzer exited zero on the twenty-record archive and
produced the local JSON above. `python3 -m py_compile` on the analyzer,
`sh -n` on the three policy runners, `git diff --check`,
`make checkpatch`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .` (12 HTML pages) and
`python3 scripts/site-faq.py --check` exited zero. No new production
module code was introduced, so the full VM regression suite was not
repeated for this documentation and measurement commit.
