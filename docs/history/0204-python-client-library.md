# 0204 — Python client library, phase 1 (C5)

Date: 2026-10-26. Status: phase 1 of roadmap item C5 done on a local branch: the package, its
tests and the suite step; the kernel backend checked on the dedicated Debian host. Phase 2 (the
LangGraph store on the library, G1's gate carried over) not started.
Work started: 2026-10-26 16:35 America/Sao_Paulo, after G5 was merged and the maintainer said
to go on. Ended: 2026-10-26 17:26. Wall-clock time, not an engineering-effort measurement. C5
is estimated M–L; this phase M.

## Goal and decisions

ROADMAP C5: an installable, versioned Python package with capability detection and the scan
fallback, from G1's adapter. Three decisions, recorded in
[ADR-0023](../decisions/0023-python-client-library.md):

- the maintainer chose **LGPL-2.1-or-later** for `python/` (the repository stays GPL-2.0-only),
  so a memory system under any licence can import it;
- the maintainer chose the name **`moobfs`**, installable with `pip` from the repository and
  not published on PyPI;
- the scan applies the **kernel's rules** (matching, ordering, homogeneity), so its answers
  equal `QUERY2`'s; a fallback that returned every file the caller can open, as the G1
  adapter's does, was rejected because it disagrees with the kernel for heterogeneous files.

## Changes

- `python/`: `pyproject.toml` (PEP 621 and 639), `LICENSE` (LGPL-2.1), `README.md`,
  `build-requirements.txt` (setuptools 84.0.0 by hash) and the package:
  - `_uapi.py`: ctypes mirror of `moob_query_term`, `moob_query_result`, `moob_query2`,
    `moob_query2_batch`, `moob_info` and the ioctl numbers;
  - `attrs.py`: `Value`, `set_attr`, `get_attrs`, `name_ok`, `validate`, with the rules of
    `common/attr.c`;
  - `query.py`: `Term`, `Order`, `QUERY2` with every page followed, `QUERY2_BATCH` that uses
    outputs only after a zero return (spec-query-v2 §4.3), `QueryRefused`,
    `KernelUnavailable`;
  - `scan.py`: the walk with `reach_ok`, `target_ok` and `ext_acl` of `layer/index.c`;
  - `domain.py`: `layer_mounted`, `get_info`, `usable_info`, `Domain` with backends `auto`,
    `kernel` and `scan` and `on_refusal`;
  - `publish.py`: the write sequence of contract §6, `file_mode_for`, `RENAME_NOREPLACE`.
- `python/tests/`: `test_unit.py` (anywhere), `test_uapi.py` (compiles a C program against the
  header), `test_linux.py` (a plain Linux directory), `kernel_equal.py` (a mounted layer, root).
- `make pylib-test`, in the CI job "Tools and sanitizers": a fresh venv, setuptools by hash,
  `pip install --no-build-isolation` from a copy under `$(BUILD)`, then the tests against the
  installed package (one test checks that the import comes from `site-packages`).
- `vm/suite.sh`: step `pylib`, which runs `kernel_equal.py` when the guest has `python3`, and
  prints `PYLIB_SKIPPED` otherwise. The KASAN guest boots the host's root and runs it; the
  `vm-test` initramfs has no Python and skips it. `kasan.yml` now also runs for `python/`.
- README (both languages), ROADMAP C5 row, `docs/testing.md`, contract §7 (implemented note),
  ADR-0023, `docs/references.md` §12, `docs/retired-claims.txt` ("no library yet", "ainda sem
  biblioteca"), changelog, history index.

## Evidence

`kernel_equal.py` on the dedicated Debian host (kernel `6.12.111+deb13-amd64`, ext4 lower,
`domain_depth=1`, module built with `make layer` from this branch; the layer is unchanged since
`c793f8d`), Python 3.13.5, as root, rerun after the last change to the tests:

```text
MOOBFS_PYLIB_PASS: 400 requests (255 with answers in alice) x 2 domains x pages 7 and 4096,
60 batches: kernel = scan through the mount = scan of the lower; owner and team member agree;
another uid refused by both; a refused query raises QueryRefused or is answered by the scan
```

The corpus is 600 files in `alice` (`0700`, uid 2001) and `team` (`2770`, group 2100), every
type, absent attributes, raw values, invalid values written on the lower (the layer refuses
them through the mount, `EINVAL`), heterogeneous files (`0200`, `0604` in the group domain),
hard links, two levels of subdirectories, a directory and the domain itself carrying an
attribute. Requests: 0 to 4 terms of every operator, `missing_ok`, mismatched types, and
orderings of the four orderable types, ascending and descending.

Negative rounds, on the host copy, restored afterwards: with the scan's `nlink > 1` rule
removed, and with its "the domain lets this class through" check removed, the test printed
`MOOBFS_PYLIB_FAIL` with the first differing request each time.

`make pylib-test` in a `gcc:14` container (Debian, Python 3.13.5, amd64 emulated on the
Apple Silicon laptop), as root: 18 tests, OK, the installed package imported from the venv, the C
layout test not skipped. The unit tests also pass on macOS with Python 3.15.0 (the Linux ones skipped).
`shellcheck` 0.9.0 passed on `vm/suite.sh`.

## Errors found on the way

- **Publishing as root into another owner's private domain left the file heterogeneous.** The
  first version, copied from the G1 adapter, set the domain's group only when the domain grants
  its group access. Root gave a file in `alice` (`0700`, uid 2001) owner 2001 and group 0, which
  fails condition 2 of the rule, so `QUERY2` and the scan both left it out. The test caught it
  only as "too few requests have answers", 21 of 400, and the guard that fails a weak run was
  added for that reason. Fixed: the owner or root gives the file the domain's group too. The
  G1 adapter in `bench/client/` has the same logic; its gates published as the domain's owner,
  so they were not affected, and it is left as it ran.
- The first random generator produced 8 requests with answers out of 400; requests now draw
  values from the corpus and use fewer terms.
- First CI run of the pull request, two failures, both in the tests. "Tools and sanitizers":
  `test_linux.py` moved a file into a `0600` directory, which only root can do; the file is now
  published there before the directory is closed, and the test passed as an ordinary user and
  as root. KASAN: the `pylib` step was placed after the suite unloads the module, so its mount
  found no `moobfs_layer`; it now runs before the unload.

## Limitations

- Phase 2 is open: the LangGraph store on the library and G1's gate run with it. Until then
  the roadmap's first acceptance criterion is not met and C5 is not complete.
- One host, ext4 only for the kernel comparison, x86-64. btrfs and XFS lowers, arm64 and the
  KASAN kernel get the `pylib` step in CI only once this branch runs there.
- The scan's known edges (`moobfs/scan.py`): attribute lists over 64 KiB, more than 64 levels
  below the mount root, domains the kernel answers in fallback mode.
- Without `order_by` the order is unspecified on both backends; the scan returns inode order.
- The scan costs a `stat` and the attribute reads of every file of the domain per query.

## Next step

Phase 2: `moobfs.langgraph` with the LangGraph store on the library, and G1's mounted gate run
with it on the dedicated host once the fstests runs of issue 142 have finished there.

## Checks of this commit

Completed 2026-10-26 at 17:26 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`),
`python3 scripts/check-site.py .` (12 pages), `python3 scripts/site-faq.py --check`,
`shellcheck` 0.9.0 on every `vm/*.sh` and `scripts/*.sh` (as `make lint`) and `py_compile` of
the package and tests passed. A local check resolved 709 relative Markdown destinations,
anchors included, in the eleven changed documents. No kernel code changed; the `pylib` step
under KASAN runs in the pull request's CI.
