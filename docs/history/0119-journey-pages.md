# 0119 — The journey on the site, in English and Portuguese (ADR-0015)

Date: 2026-10-17. Started 18:30, ended 19:40 (calendar time, local). Status: pages written and
link-checked locally; ADR-0015 accepted by the maintainer the same day. Estimated effort: S.

## Goal

A lasting public record of the project's evolution, readable by researchers, engineers and the
community, in English and in Portuguese, as the maintainer asked on 2026-10-17.

## Decisions

- Proposed in ADR-0015: four generated pages (journey, status, research topics, how the project
  is made) with Portuguese counterparts under `site/pt/`, as the one exception to ADR-0010 besides
  `README.pt-BR.md`. Alternatives (a separate site, translated journals, English only) in the ADR.
- The milestones were picked from the journal index: 16, from the first question (journal 0001)
  to syzkaller (journal 0112). The status page follows the questions of
  `Documentation/filesystems/adding-new-filesystems.rst`. Research topics come from measured gaps
  (ordered traversals, the user-space alternative), the security model (labels in the partition
  rule) and the stacked-filesystem obligations.
- Content of the study of comparable projects that led here stays outside the repository
  (`AGENTS.md`); only what the pages state is here, each statement linked to a journal or ADR.

## Changes

- `scripts/site-journey.py` (new): both languages side by side; writes the eight pages.
- `site/`: the eight pages, styles for them, a "Journey" link on the existing pages, the
  sitemap.
- `docs/decisions/0015-journey-pages-in-two-languages.md` (accepted the same day).

## Results

- `python3 scripts/site-journey.py .` then `python3 scripts/check-site.py`: `Site links valid: 12
  HTML pages`.
- Not checked: the rendering in a browser at phone width; the Pages deployment (runs only on a
  merge to `main`).

## Limitations

- The status page states facts as of 2026-10-17; it is updated with each batch.
- The Portuguese navigation links to the English documentation pages, marked as English.

## Next step

A pull request after the stabilization batch, so
the status page can name what that batch tested.
