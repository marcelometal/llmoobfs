# 0122 — Every public surface kept in step with the project's state

Date: 2026-10-18. Started 08:40, ended 09:00 (calendar time, local). Status: rule added to
`AGENTS.md`, inventory written. Estimated effort: S.

## Decision

The maintainer asked, as a standing rule, that the many places stating the project's state be
updated together, so that nothing is left disconnected. `AGENTS.md` now says that a change of state
updates every place that states it, in the same unit of work and in both languages where a surface
has two; `docs/public-surfaces.md` is the inventory to walk (repository files, the site and its
generated pages, GitHub issues, Discussions, releases and advisories, and, by kind only, the
surfaces outside GitHub whose content stays out of the repository).

A surface left stale on purpose is named in the journal of the change with the reason. Example at
the time of writing: the README's "no outside review yet" is to change when the reviews of
journals 0111 and 0114 are published, together with their fixes (coordinated disclosure).

## Alternatives

- A script that compares the surfaces. Useful later for numbers and dates (the latest tag, the
  journal count, the review status); not written now, since most surfaces state facts in prose.

## Next step

Walk the inventory when the stabilization batches are published.
