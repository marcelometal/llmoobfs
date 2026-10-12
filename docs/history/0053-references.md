# 0053 — Verified references

Date: 2026-10-12. Status: done on branch `docs/references`; pull request after the Q2e pull
request (51) lands.

## Goal

The maintainer asked that the scientific papers and projects helping MoobFS be cited in the
repository. The research documents held about 190 citations, scattered and mixed with analysis;
the README cited only the design basis of v1.

## Decisions

- One page, `docs/references.md`, in four groups because the obligations differ: code included in
  the repository (provenance and license), research that shaped a decision (each work tied to the
  ADR, specification or journal where it mattered), systems and kernel facilities, and the agent
  memory clients analyzed. Works only surveyed, and tools only planned, stay in the research
  documents.
- Every entry was checked against its primary source (Crossref DOI metadata, publisher or USENIX
  page, official repository, kernel documentation) by two parallel research agents; four DOIs were
  re-checked by hand against Crossref.
- The README "Prior work" section now names the research behind the domain-scoped `QUERY2` and
  links the page; `README.pt-BR.md` mirrors it.

## Corrections made in the repository

- `docs/research-academic-survey.md`: the Micheli, Margaritis and Anastasiadis titles were wrong;
  now "Lethe: Cluster-Based Indexing for Secure Multi-user Search" (IEEE BigData Congress 2014) and
  the journal version "Permission-based Index Clustering for Secure Multi-User Search".
- `docs/research-metadata-search.md`: the Lemire, Boytsov and Kurz journal version, recorded as not
  confirmed, is confirmed (Software: Practice and Experience 46(6), 2016).
- `tests/third_party/fsx/README.md`: the license statement pointed to the header of `fsx.c`, which
  carries only the original NeXT copyright; it now names the xfstests project license.
- `docs/ecosystem.md`: the sentence "all of the below are Apache-2.0, Python" was wrong (Agent VFS
  is MIT, ai-memory is Rust); xfstests now links the canonical kernel.org repository; virtme-ng is
  GPL-2.0-only.
- `docs/research-academic-survey.md`: a dated re-check note on the Elasticsearch quote; the
  current documentation page states that restricted documents still count in scoring statistics,
  and the quoted "count" wording was not found again on that page. ADR-0009 is left as written.

## Limitations

- Two page ranges could not be confirmed and are omitted (Büttcher and Clarke, FAST 2005; GUFI's
  SC22 article number).
- The SHAs of four GitHub Actions and the Debian kernel package pin were not re-verified.
