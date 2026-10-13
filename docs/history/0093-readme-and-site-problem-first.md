# 0093 — README and landing page open with the problem, the numbers and the demonstration

Date: 2026-10-13. Status: done locally on a worktree branch; not pushed. Documentation and site
only: nothing under `layer/`, `kernel/`, `include/`, `vm/`, `bench/`, `tools/`, `tests/` or
`.github/` changed. `docs/history/README.md` and `CHANGELOG.md` are left for the maintainer to
update at merge.

## Goal

A newcomer should understand in thirty seconds what problem the project addresses, what it does,
what was measured (including where it loses) and how to help. Both `README.md` and
`site/index.html` opened with the mechanism and a block of caveats.

## Decisions

- **Order of the first screen**, in both files: the problem; what the project is, as three named
  parts (typed attributes on ordinary files, an index in the kernel, a query scoped by directory
  and authorized by file permissions); four numbers; the demonstration; the status; how to help.
  Below it: how it works, what was measured, try it in a VM, limitations, documentation,
  licence. The structure follows the page and README of a related research project (YoloFS,
  read on this date); none of its text, figures, numbers or visual design was used.
- **The four numbers**, all from the M4 report draft (journal 0088) and the journals it cites:
  1. 0 differences between `QUERY2` and one real client's own SQLite, 150 to 60 000 memories
     (journals 0064, 0068);
  2. 5 to 6.5 times faster on one of that client's query shapes, with the note that part of the
     gap is the client's schema (journal 0068);
  3. 0.71 to 0.51 of SQLite's speed on its long ordered traversal: a loss (journal 0068);
  4. 19 s to mount 60 000 files with a cold cache on an SSD, 0.7 s warm, 126 s on a cloud disk:
     a cost (journal 0055).
  One correctness result, one win, one loss, one cost. The win and the loss are separate
  entries of the same size so that neither sits in the other's small print. The mount time was
  preferred to the memory per file as the cost because it is the one a user meets first and the
  report's conclusion 4 names both as the price of the layer; the memory figure is in "What was
  measured" in both files.
- **Not used: the count of defects found by the project's own tests.** The sources disagree with
  the candidate wording (journal 0087 says eleven defects, the changelog says three days, and
  journals 0082 and 0083 record that some were found from outside inspection reports). The
  status says "eleven" with the changelog as source and makes no headline of it.
- **Alternative discarded: one entry holding both the win and the loss**, to make room for the
  memory figure. Two figures in one cell read as one mixed result.
- **Section 2.1 of the report is pending.** No latency per operation of the current module is
  quoted. A comment marked `PENDING_Q2_CAMPAIGN` stands where it will go, in the "What was
  measured" section of both files.
- **Caveats.** None was removed. The short status near the top keeps six; the rest moved to
  "Limitations" (the list of journals with defects found and fixed, the refusal of
  user-namespace and idmapped mounts). Three statements were no longer true after the M4
  campaigns and were restated, not deleted: "No speed or efficiency claim ... one cloud VM", "no
  comparison with SQLite ... may be stated as a result before M4" and "no measured advantage"
  now say that only pilot-grade figures exist and that no claim beyond the stated conditions
  may be made.
- **Demonstration.** Taken verbatim from the transcript of the VM suite's demonstration step.
  The README shows its first 24 lines (the two closing `GET_INFO` calls left out, as before; the
  earlier README left out one, because the step then had one). The landing page shows a subset
  in order (preparation, mount, the four attribute writes, Alice's own domain, Bob's domain
  refused, the mount-wide query refused, root's answer) and links to the README for the shared
  directory. It is a static block, readable without JavaScript; no script was added.
- **The date of the transcript is no longer stated in the README**: the capture date of the new
  transcript was not known to this session.
- **Hosts are described, not named** ("a dedicated machine with a SATA SSD", "a cloud disk").

## Changes

- `README.md`: new first screen; `How it works` now also holds the former "At a glance" and
  "Project goal" text and points to `client-contract.md` and `adoption-risks.md`; new `What was
  measured`; `Quickstart` merged into `Try it in a VM`, with `MOOB_ONLY=demo make vm-test`; a
  `Citing` section pointing to `CITATION.cff`; two rows added to the documentation map. The
  diagram names `QUERY2` on a domain directory.
- `site/index.html`: same order. The diagram moved from the top to "How it works". The evidence
  figures "0 performance numbers" became "0 outside reviews". The roadmap list gained `QUERY2`
  and the M4 report in progress. Section ids `purpose`, `roadmap` and `documentation` are kept.
- `site/style.css`: rules appended for the links row, the transcript, the numbers and the three
  parts. No existing rule changed except `.hero { align-items: start }`.
- `site/faq.html`, `site/plan.html`: one phrase each ("no performance numbers", "the M4
  benchmark has not started and no performance number exists") restated.

## Links and anchors

- Links into `README.md` found in the repository: `README.pt-BR.md` to `#try-it-in-a-vm`
  (heading kept, link works). Links into the landing page: `index.html#roadmap` (four) and
  `index.html#purpose` (one, from `guide.html`); both ids kept.
- README headings removed: `Quickstart`, `At a glance`, `Project goal`. Nothing in the
  repository links to them; a link from outside to those anchors lands on the top of the page.
- `docs/history/0088-m4-report.md` is linked from both files and **does not exist on this
  branch**; the links work once the report is merged.

## Evidence

```text
$ python3 scripts/check-site.py
Site links valid: 4 HTML pages
```

- Relative links and anchors of `README.md` checked with a throwaway script: the only target
  missing is the report above.
- The landing page was rendered with headless Chrome at 1280, 900 and 500 pixels wide and read:
  no horizontal overflow, the transcript wraps inside its block.
- The page loads `style.css` and `favicon.svg` only; no script, no external resource.
- Not run: `make vm-test` (nothing it tests changed); the Pages workflow; a screen reader.

## Limitations

- `README.pt-BR.md` still has the old order and the old status text.
- `CITATION.cff` still names `v0.1.0-experimental`.
- `ROADMAP.md`, `docs/faq.md`, `docs/when-not-to-use.md` and `site/faq.html` beyond the one
  phrase may still carry statements written before the M4 campaigns; they were not reviewed.
- The existing comparison section of the README names three memory frameworks; it was kept as
  it was.
- The numbers on the first screen must be revised if the report changes before it is merged.

## Next step

Fill the `PENDING_Q2_CAMPAIGN` place in both files when section 2.1 of the report is recorded;
bring `README.pt-BR.md` and `CITATION.cff` into line.
