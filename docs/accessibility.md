# Accessibility of the project site

Audit of `site/` (issue #12), 2026-10-18: the four original pages and the eight journey pages of
ADR-0015 (English and `site/pt/`). Checked from the HTML and the stylesheet; no screen reader or
browser audit tool was run (a limit of this audit).

## Enforced on every change

`scripts/check-site.py`, run by the CI job "Tools and sanitizers" and by the Pages workflow, fails
when a page has no `lang` on `<html>`, not exactly one `<h1>`, no `<header>`, `<main>` or `<nav>`,
no skip link, an image without `alt`, or an SVG that is neither `aria-hidden="true"` nor
`role="img"` with a name. All twelve pages pass.

## Contrast (WCAG 2.2, 1.4.3 and 1.4.11)

The colours are OKLCH tokens in `site/style.css`; the ratios below were computed from them.

| Text on background | Ratio | Level |
| --- | --- | --- |
| ink on white | 17.4 | AAA |
| muted text on white | 7.5 | AAA |
| links (primary) on white | 7.1 | AAA |
| white on primary (button) | 7.1 | AAA |
| white on accent (code blocks) | 16.1 | AAA |
| transcript text on accent | 10.4 to 10.9 | AAA |
| muted text on surface (inline code) | 6.7 | AA |
| ink, muted and links on the status box (wash) | 6.1 to 14.9 | AA |
| plan: text of a done box on its background | 8.4 | AAA |

Not a finding: the 1 px separators (`--line`, 1.7:1) are decorative, which 1.4.11 exempts; the
state of a pressed milestone button and the focus outline use the primary colour (7.1:1).

## Other points

- Focus: every link, button and summary has a 3 px outline in the primary colour.
- Images: the data-flow diagram of the landing page is `role="img"` with a title and a
  description; the logo marks are decorative (`aria-hidden`).
- Motion: the site has no animation or transition.
- Language: each page declares its language and links to the other one with `hreflang`; the
  Portuguese navigation says which linked pages are in English.
