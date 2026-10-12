# 0004 — LLM memory and context as an explicit goal

Date: 2026-10-10, America/Sao_Paulo.

## Request

The user observed that the presentation emphasized deduplication and sharing,
without making the focus on memory/context for LLMs sufficiently clear. They asked to
adjust the README and the site, distinguish goal from implementation, record and publish.

## Editorial decision and justification

Present MoobFS first as an experimental native filesystem aimed at the persistent
memory and context consumed by LLM-based agents. Byte sharing
remains as the technical foundation demonstrated by M1. Keeping the previous explanation
as the opening would preserve the ambiguity of a generic filesystem; therefore
the project's intention now appears before the mechanism.

Make responsibilities explicit: the filesystem stores and exposes files; the agent's
application selects material and assembles the context. Semantic retrieval and integration
with agents remain future work. No change to the format, the adopted architecture
or the native code; ADRs 0003 and 0004 remain valid. No new features,
context window enlargement or performance gains are promised.

## Changes

- `README.md` and `README.pt-BR.md`: goal in the opening and a separate section for M1.
- `site/index.html`: title, metadata and hero focused on LLM memory/context;
  goal, responsibilities and implemented status explicitly separated.
- `site/guide.html`: places the reproduction as a test of the storage base,
  without confusing it with a demonstration of memory consumption by an LLM.
- `PRODUCT.md`: keeps this positioning in the site's continuity instructions.
- `CHANGELOG.md` and the history index updated.

## Validation and publication

`python3 scripts/check-site.py` passed for both pages; `git diff --check`
passed. Preview inspected in the browser on desktop and narrow screen: readable title,
navigation to the guide working and no horizontal overflow measured on both pages.
The native code did not change; local kernel tests were not repeated for this
editorial change. The PR's required CI and the Pages workflow record their
runs on GitHub, linked to the published commits.

Publication was explicitly authorized in the request. The delivery uses a PR to `main`
and the existing GitHub Pages workflow; the previous history remains preserved.
