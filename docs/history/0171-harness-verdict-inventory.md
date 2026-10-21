# 0171 — Preserve harness verdicts and state the actual test inventory

Date: 2026-10-21. Status: in progress; local preparation only.

Started: 2026-10-21 08:59 UTC. Calendar time, not measured labor effort.

## Goal and decisions

Prepare issue 114 T7/T11 on top of `f5c131a`, without modifying PR 109,
publishing or implementing the other test gaps. Keep one local batch with small
signed commits. No module runs on the host.

`race2` printed its summary before checking the forked query process. Wait for
that process first and only let the successful parent print a summary. Capture
command output and status before displaying it in the POSIX shell harness;
`pipefail` is not portable. Apply this to race2, race, buildrace and the
benchmark-oracle correctness check. Preserve racer stop/wait and scan-delay reset
on failures. Other query/grep pipelines are outside this narrow correction.

## Validation in progress

Controlled producer and fork-child failures, positive controls, lint, checkpatch,
document checks and the full Docker/QEMU VM suite are pending. A build failure
will not count as negative evidence. Logs will remain outside Git.

## Scope and next step

T5/T6/T8/T9/T10 and the remaining issue 114 work stay open. No kernel logic,
UAPI, feature or new policy changes. Reconcile documentation with actual markers,
skips and compiled-only programs, then record executed results here.
