# 0209 — A faster check cycle: focused steps per correction, the full suite per batch

Date: 2026-10-27. Status: done on a local branch. Work started and ended on 2026-10-27, in the
afternoon (America/Sao_Paulo), at the maintainer's request before the security batch. Effort S.

## Goal and decision

On 2026-10-27 most of the waiting was the KASAN job: each of PRs 151, 156 and 158 changed only
Python and still waited about 45 minutes for it, and the rule for kernel corrections asked for a
complete `make vm-test` per correction. The maintainer asked for a quicker way that keeps the
same assurance before a merge, and approved this one:

- **Per correction**: `make checkpatch`, the suite steps the correction touches
  (`MOOB_ONLY=...`) and the negative round on those steps. A correction that touches locking or
  object lifetime also runs those steps under KASAN/lockdep at once.
- **Per batch, before publication**: the full `make vm-test` and the full local KASAN/lockdep
  suite, then one pull request.
- **CI**: the KASAN job runs the suite only for kernel-side paths (`layer/`, `vm/`, `include/`,
  `common/`, `tests/`, `sqlite/`, `tools/`, the `Makefile`, the workflow). `python/` and
  `bench/` leave it; the library keeps `make pylib-test` in "Tools and sanitizers". The built
  kernel is cached.

The required checks of `main` do not change.

## Measured cost of the job before the change

PR 158's KASAN job, 2026-10-27: dependencies 5.5 min, kernel configure and build 16.2 min,
module and test binaries 0.3 min, then the suite (about 20 min on earlier runs).

## Changes

- `AGENTS.md`: the rule for kernel corrections, as above. The previous wording asked for
  `make vm-test` and a negative round per correction and local KASAN once per batch.
- `.github/workflows/kasan.yml`: the path filter without `python/` and `bench/`; a cache of
  `build/linux` with `actions/cache` v6.1.0 (pinned by commit). The key is the kernel version,
  the compiler version, the runner image and the hash of `vm/kasan.config` and
  `vm/kasan-requirements.txt`. On a hit, the fetch and the kernel build are skipped. GitHub lets
  a pull request read only caches saved on the default branch or on its own branch, so one pull
  request cannot plant a kernel that another one uses. The cache on `main` comes from the weekly
  run or a manual one.
- `CONTRIBUTING.md` (the paths that run the job, now with `sqlite/` and `tools/`, which the filter
  already had), `docs/testing.md` (the KASAN row and the `PYLIB_SKIPPED` row), the changelog and
  the history index.

## Validation and limitations

`actionlint` 1.7.7 accepted the workflow. The cache saves on the first run of a key and is reused
from the second. This pull request's own run builds the kernel and saves the cache on its
branch; `main` gets its entry from a manual run after the merge. A cache entry larger than
GitHub's limit is not saved, and the job then builds as before; the size is recorded once
measured. A suite step that a correction does not touch can still regress between batches; the
full runs per batch and in CI catch that before a merge.

## Next step

Security batch 1: issues #113 and #118, under this cycle.
