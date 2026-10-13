# 0087 — Third pre-release tag: v0.3.0-experimental

Date: 2026-10-13. Branch: `release/v0.3.0-experimental` on top of `main` (`fe594c1`).

## Decision

Tag `v0.3.0-experimental` and publish a GitHub pre-release, without binaries, packaging or
support, as the maintainer decided on 2026-10-13. This replaces the decision of the same day not
to make a patch release of `v0.2.0-experimental` (journal 0080): that tag carries eleven defects
fixed since, four of them security ones, and the project is about to ask for outside attention
(journals 0081 and 0084). Nobody should be pointed at a tag with known security defects.

It is a new minor version and not a patch because it also changes behaviour: nested btrfs
subvolumes are refused, an invalid format marker refuses the mount, `GET_INFO` counters are
reserved to the administrator (a UAPI change), and unmount waits for a sync of the lower.

## Changes

- `CHANGELOG.md`: the "Unreleased" entries become the `v0.3.0-experimental — 2026-10-13`
  section, with an opening summary of what was fixed, what changes for a user and what stays
  open; the entries do not change. An empty "Unreleased" heading stays on top.
- `layer/main.c`: `MODULE_VERSION("0.3.0")`.
- `README.md`, `SECURITY.md`, `CONTRIBUTING.md`, `docs/compatibility.md`: name the current tag
  and say that the two earlier ones carry fixed defects.

## Evidence

The checks of this branch's pull request are the validation, the KASAN job included (the branch
touches `layer/main.c`). The tag is created on the merge commit in `main`; the release notes are
the opening summary of the changelog section.

## Limitations

A source snapshot of a research project: one kernel series, x86-64, tested in a VM; no outside
review has happened yet (requested on this date). The M4 report is not part of this tag.

## Next step

The M4 report (journal 0088, in progress), then the second client.
