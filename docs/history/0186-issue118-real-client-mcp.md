# 0186 — Real ai-memory MCP workload with the issue-118 guard

Date: 2026-10-24. Status: bounded real-client run complete; policy decision open.
Work started and ended: 2026-10-24. Labor time: not tracked separately.

## Goal and decision

After opening PR 136 for the delayed-lower-I/O measurements, the maintainer
asked to continue with a real client workload. The earlier 60,000-page
ai-memory replay in journal 0182 used the client's bulk import and reindex
path. This round uses the existing `memory_write_page` MCP path and the
client's own SQLite writes, followed by the existing typed-attribute mirror
and six-shape SQL-versus-`QUERY2` comparison. Use ten projects with 200
requested pages each, a bounded scale previously exercised before the
experimental guard. A unique ext4 disk image in a dedicated native-KVM VM
keeps the legacy `stage2.sh` cleanup confined to that disposable disk.

## Plan and safety

`vm/issue118-client-mcp-host.sh` refuses a reused run ID, checks for an
existing QEMU process, builds the current module and query helper, then
creates a fresh labelled 2 GiB sparse image. The guest verifies the label
before mounting it, loads only the VM module and runs `stage2.sh` with
`BULK=0` and no latency binary. The workload must report 2,000 successful
MCP writes and zero refusals, the mirror must find no missing files, and
the comparator must report zero failed checks. No host module is loaded.
Record the exact client and module revisions, output markers and artifacts
after the run. This is a correctness and compatibility check on this one
client/host, not a latency estimate or a slow-device measurement.

## PR 136 initial lint correction

The first CI run reported a lint failure. Its completed log showed
ShellCheck SC1007 for `CDPATH= cd` in the policy host and pairs scripts;
the assignments were made explicit as `CDPATH='' cd` in those scripts and
the new MCP scripts. The CI stopped before the SPDX check. A separate
local `sh scripts/check-spdx.sh` found the new archived-pair analyzer
missing its header; that header was added without changing the analysis.
Rerun both lint checks before treating PR 136 as validated.

## Pilot and full run — 2026-10-24

The host checkout was clean at `1436c59`, with no QEMU running and the
pinned ai-memory binary present at `385b81e`. A bundle with prerequisite
`1436c59` was SHA-256 verified on both machines and fast-forwarded the
host checkout to `51a46d3`; no public push was needed for the test setup.
`sh vm/issue118-client-mcp-host.sh mcppilot2x10 2 10` completed in a
disposable VM with `W-AIM: projects=2 pages=20 written=20 refused=0`,
`MIRROR: pages=20 mirrored=20 missing_files=0`,
`COMPARE: projects=2 checks=12 failed=0`, and `AIMEM_MCP_PASS`.

The next unique image, `mcp10x200`, ran ten projects and 200 requested
pages per project. All 2,000 MCP calls succeeded with zero refusals in
573.4 seconds. The client added one automatic profile page in an extra
project, consistent with journal 0068. The mirror covered all 2,001
database pages with zero missing files in 0.3 seconds (87 µs median per
page); the existing comparator completed 66 SQL-versus-`QUERY2` checks
across eleven projects with zero failures. All six result shapes were
nonempty: incremental 844, pinned 200, recent 201, rules 157, settled
506 and stale 200 returned paths in aggregate. The guest emitted
`AIMEM_MCP_PASS` and the runner exited zero.

Both VMs used host checkout `51a46d3`, client source `385b81e`, and the
host's Debian `6.12.111+deb13-amd64` kernel. The full run built and loaded
the current module from that checkout inside the VM. Afterward, QEMU count
was zero and the dedicated host checkout was clean. The fresh labelled
disk images remain in their unique run directories on the host. Selected
environment files, console logs, workload CSVs and comparison outputs
were copied outside Git to
`MoobFS-local/issue118-client-mcp-20261024/mcp-logs.tar.gz`; its SHA-256
was verified against the host archive as
`ee0f2f5d2010c9625d027a71494809dffed2b48273406ed7d98dbef795b16aba`.
The archive omits the client server log and disk images. Reproduction on
the dedicated host uses
`sh vm/issue118-client-mcp-host.sh NEW_RUN_ID 10 200` with a new run ID.

This is evidence that the experimental guard admits this real MCP client
shape at 200 pages per project and preserves the six query answers. The
largest tested project here is far below the 17,000-file denial boundary,
so the result does not settle that compatibility tradeoff. The 573.4-second
write time includes the client's Git checkpoints and this VM; it is not a
controlled MoobFS-versus-ext4 latency comparison. The lower device was a
fresh ext4 image on the same host, without synthetic read delay or a
physical slow-device test. The query page cache was not evicted before
each shape. The issue remains open for a policy decision; no production
limit or on-disk format changed.

`sh -n` on the four issue-118 runners, `sh scripts/check-spdx.sh`,
`git diff --check`, `python3 -m py_compile` on the archived-pair analyzer,
`make checkpatch`, the retired-claims check, the 12-page site check and
the generated FAQ check all exited zero after the ShellCheck syntax fix.
Local ShellCheck was unavailable; the updated PR CI is the remaining lint
gate. The full regression VM suite was not repeated locally because the
production module was unchanged; the dedicated MCP pilot and full run
above exercised the real-client path.

## Publication update — 2026-10-25

The maintainer authorized completing the previously reviewed PR 136.
Immediately before merge, `origin/main` was still `2396c16`, the PR head
was `40ed7f5`, all eleven checks had completed successfully, the PR was
mergeable, and no reviews or comments were pending. GitHub merged it into
`main` as `c2fc2df` at 2026-10-25T08:48:11Z. The merge commit's tree was
verified identical to the validated PR head with
`git diff --quiet 40ed7f5 origin/main`; the local `main` advanced by
fast-forward and was clean. Issue 118 remained open after the merge for
the compatibility and numerical-policy decision. This update records
publication of the measured code and documentation; it does not claim a
new performance threshold or a resolved issue.
