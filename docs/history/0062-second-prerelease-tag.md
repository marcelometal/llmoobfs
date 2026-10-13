# 0062 — Second pre-release tag: v0.2.0-experimental

Date: 2026-10-12. Branch: `release/v0.2.0-experimental` on top of `main` (`b3fb9fa`).

## Decision

Tag `v0.2.0-experimental` and publish a GitHub pre-release, without binaries, packaging or support,
as the maintainer decided on 2026-10-12. It snapshots the per-domain `QUERY2` series (journals 0035
to 0038, 0052), its measurements (0042 to 0049, 0055), the first real-client test (0054) with the
rename fix it led to (0057), the English repository (ADR-0010), the verified references (0053) and
the local KASAN job (0058).

Correction to journal 0026, which said "no new tag before the M4 report": the maintainer chose to
tag now. The M4 report is still planned (`ROADMAP.md`), and the release notes say that the numbers
are evidence for the design from one host, not a benchmark claim.

## Changes

`CHANGELOG.md`: the "Unreleased" entries become the `v0.2.0-experimental — 2026-10-12` section
with an opening paragraph; the entries do not change. A new empty "Unreleased" heading stays on top.

## Evidence

The checks of this branch's pull request are the validation. The tag is created on the merge
commit in `main`; the release notes are the summary in the pull request plus the changelog section.

## Next step

The local series agreed in journal 0059 (S1 and F1 done locally; stage 2 of the client test next).
