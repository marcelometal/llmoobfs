# 0186 — Real ai-memory MCP workload with the issue-118 guard

Date: 2026-10-24. Status: investigation in progress.
Work started: 2026-10-24. End and labor time: not yet recorded.

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

The first CI run reported a lint failure. Local reproduction with
`sh scripts/check-spdx.sh` identified the new archived-pair analyzer as
missing the repository's SPDX header; the header was added. This is a
metadata correction, not a change to its analysis. The other reported
compiler checks passed at this point; VM and sanitizer jobs were still
running. Rerun lint before updating the PR.
