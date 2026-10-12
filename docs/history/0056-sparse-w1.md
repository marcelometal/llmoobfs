# 0056 — Static analysis: sparse and W=1 on both kernel modules

Date: 2026-10-12. Status: run locally; no change needed; not yet in CI.

## Goal

Item S5 of `docs/security-roadmap.md` asks for `sparse`, `smatch` and `W=1` over the kernel code, after
checkpatch was brought to zero warnings and made required (journal 0050).

## Method and results

In the throwaway build container of `make vm-test` (Debian kernel headers for 6.12.111), on the tree
of branch `m3/query-v2-q2e` updated with `main`:

- `sparse` 0.6.4 (`make -C <kdir> M=layer C=2 modules`, then the same for `kernel/`): it checked
  `layer/main.c`, `layer/attr.c`, `layer/index.c` and the M1 module, and reported nothing. A control
  module with a planted `__user` cast was flagged ("cast removes address space '__user'"), so the
  checker was active.
- `W=1` (extra warnings, including `-Wmissing-prototypes` and `-Wmissing-declarations`, confirmed in
  the verbose command line): no warning in either module.

## Limitations and next step

- Not in CI yet: adding `sparse` and a `W=1` build to the `Lint` or the VM job is the next step of S5,
  together with `smatch` (not run) and `kernel-doc` for the UAPI header.
- The S5 status line of `docs/security-roadmap.md` will be updated after the English translation of
  that file (PR 46) lands, to avoid a conflict with it.
