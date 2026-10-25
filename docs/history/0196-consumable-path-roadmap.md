# 0196 — Consumable path on the roadmap; kernel freeze restated (ADR-0022)

Date: 2026-10-25. Status: documentation complete on branch `claudiofp/CONSUMABLE`, not
published. Work started and ended: 2026-10-25, one session. Labor time was not measured.

*Numbering note:* journal 0194 is on the evolution branch (`claudiofp/EVOLUTION`, not yet
published) and that front's next journal is 0195, so this record takes 0196. If the core front
publishes a journal with the same number first, this one is renumbered at merge.

## Goal

The maintainer asked how the module becomes useful, viable and consumable without barriers,
before plugins and further evolution, and asked for that to become a roadmap. The stabilization
rule ("no new features") had become unclear now that two other fronts run in parallel: one
matures the core, one runs the evolution track G1–G6.

## Decisions

- **ADR-0022.** The kernel freeze stays for `layer/`, the UAPI and the format. User-space work
  toward a consumable module may proceed, item by item, with the maintainer's go-ahead.
  Installable packages are prepared and tested in VMs, not published, until the stage 2 gates of
  the distribution strategy pass. Alternatives considered and rejected: keeping the total
  freeze, lifting it, publishing DKMS packages as a preview now, and folding the path into
  G1–G6 only. The reasons are in the ADR.
- **Roadmap section "Consumable path", items C1–C7**, with priority, effort, scope and
  validation, and a definition of done for the whole path (a clean Debian 13 VM, someone who did
  not write the documents, 15 minutes, two OS users, clean uninstall).

## What already existed and was reused

- `docs/distribution-strategy.md` (proposal, 2026-10-10, dated note 2026-10-25): DKMS,
  packages, signing, `moobfs` CLI, container pattern §2.5, stages 1–3 §8. C2, C3, C4 and C6 take
  their scope and acceptance from it.
- `docs/adoption-risks.md`: risks 5 (Linux, module and root), 6 (out-of-tree module), 10 (small
  query language, client library) and 11 (homogeneity rule and deployment recipe).
- The G1 adapter package in `bench/client/` (journal 0189), which C5 promotes.
- ADR-0013 (scan fallback, SQLite virtual table) and the client contract.

New here: C1 (first contact: the README opens with a long running-status paragraph that a new
reader must cross before learning what the project is) and C7 (an optional agent adapter after
the library). C2 notes that the `Makefile` has no `install` target.

## Changes

- `docs/decisions/0022-kernel-freeze-and-user-space-consumable-track.md` (new).
- `ROADMAP.md`: the "Consumable path" section; the agreed-sequence sentence on new features
  points at ADR-0022; the "Public release" row says packages may be prepared under C6.
- `docs/references.md`: OpenZFS/`zfs-dkms`, DKMS and the kernel's module-signing document, cited
  by ADR-0022.
- `docs/history/README.md`, `CHANGELOG.md`.

## Public surfaces

Walked `docs/public-surfaces.md`. The READMEs, the site and the GitHub surfaces state no
consumable path and no change of tested state, so they were left as they are. The landing page
and `ROADMAP.md` keep the sentence "New features wait for project review and the maintainer's
decision", which ADR-0022 keeps true for the kernel; the roadmap now adds the user-space scope
next to it. Nothing was retired, so `docs/retired-claims.txt` is unchanged.

## Tests

Documentation only: no module, VM or KASAN run was needed or done. `sh
scripts/check-retired-claims.sh` printed `RETIRED_CLAIMS_OK`, and `git diff --check` and
`python3 scripts/check-site.py .` exited zero. `make checkpatch` was not run; nothing in the
module changed.

## Limitations and next step

No item C1–C7 is started; effort figures are estimates. C5 depends on the evolution front and
C3 may surface a kernel need that only the core front can take. Next step: the maintainer picks
which item starts first. C1 and C2 are the cheapest and remove the first two barriers a new user
meets.
