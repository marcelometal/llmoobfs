# 0149 — Synchronize mutable status flags on the backing file

Date: 2026-10-20. Status: correction integrated locally; complete VM suite passed;
batch KASAN/lockdep pending.

Work started on 2026-10-20 (the start minute was not captured). The first
full-VM attempt log ended at 06:42 BRT, the correction commit was made at
06:53, and the integrated suite log ended at 07:07. These are calendar markers,
not a measurement of hands-on effort.

## Goal and decision

Fix review 0144 R1. A file opened with `O_APPEND` still appended through MoobFS
after the caller cleared the flag using `F_SETFL`; the reviewed ext4 VM returned
`AAAAZ` instead of `ZAAA` after `pwrite(..., offset=0)`.

The exact Linux 6.12.112 `fs/fcntl.c` changes the upper file's `f_flags` and
`f_iocb_flags`. `fs/overlayfs/file.c` shows the stacking precedent: before
lower I/O, compare mutable status flags, validate a change against the lower
file, and update its flags and I/O request flags together under `f_lock`.
`layer/main.c` now applies this pattern to the lower file before delegated
read, write, splice and mmap operations. This includes clearing or setting
`O_APPEND` and `O_DIRECT`, plus nonblocking status flags. The upper VFS
`F_SETFL` check remains authoritative for the caller. A lower check can
return an error to the delegated operation. No UAPI or format changes.

Alternative considered: synchronizing the lower file in `check_flags`.
Rejected because Linux 6.12.112's callback receives only flags, not the
upper `struct file`, so it cannot find the backing file. The existing lower
file is retained for the life of the upper open, so open-time copying alone
cannot follow later `F_SETFL` calls.

## Test and evidence

`tests/fcntl-flags.c` is registered in `vm/build-tests.sh` and runs for each
lower filesystem in `vm/suite.sh`. It creates a fresh file exclusively,
clears `O_APPEND` and verifies a positional write stays at offset zero, then
sets the flag again and verifies that a positional write appends. The
suite removes its file immediately afterward: the first full `make vm-test`
attempt passed this ext4 regression but later stopped at `bench-run-ext4`
because the benchmark harness requires the layer root to contain only
`bench/`, and this test had left `fcntl-flags` there. The suite cleanup was
added; this first attempt is not a full pass.
Before this correction, review 0144 reproduced the wrong result in a dedicated
Linux 6.12.112/ext4 VM. The corrected module and updated test were also built
and run together in a fresh dedicated Linux 6.12.112/ext4 KASAN-kernel VM:

```text
FCNTL_RESULT bytes=4 content=ZAAA append_flag=0
FCNTL_FLAGS_PASS
FCNTL_FIX_VM_PASS
```

The test-only `neg/fcntl-flags-test-only` branch is `80f9e81`. Its module
and test ran in a separate dedicated Linux 6.12.112/ext4 VM and returned
`FCNTL_RESULT bytes=5 content=AAAAZ append_flag=0`, confirming that the
test detects the original defect. The corrected VM's captured dmesg had no
KASAN or lockdep diagnostic according to `vm/kdiag.sh`; this was a targeted
check, not a full KASAN suite.

After adding the set-append assertion, commit `2a1570d` placed that exact
final test source on the negative branch without the module fix. A new
`MOOB_ONLY=ext4 make vm-test` run exited 2 as expected at
`FCNTL_RESULT bytes=5 content=AAAAZ append_flag=0` and
`MOOBFS_VM_FAIL: fcntl-flags-ext4`, before reaching the set-append assertion.

`make checkpatch` reported checkpatch, spelling and const-struct checks OK.
`git diff --check`, shell syntax checks and a strict C syntax check passed.
The full `make vm-test` rerun exited zero with `MOOBFS_ACCEPTANCE_PASS` and
`MOOBFS_LAYER_PASS` across ext4, btrfs and XFS after the cleanup was added.
That run contained the clear-append assertion. The set-append assertion was
added afterward. The integrated R1/R2 source snapshot `49098aa` then ran the
full `make vm-test` with exit status zero. `FCNTL_RESULT bytes=4 content=ZAAA
append_flag=0`, `FCNTL_SET_RESULT bytes=5 content=ZAAAY append_flag=1` and
`FCNTL_FLAGS_PASS` appeared on ext4, btrfs and XFS, followed by
`XATTRREAD_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`. Only VM
guests load the module. No push or pull request.

While documenting the new VM steps, the current `docs/testing.md` and
`docs/development.md` were found to still describe `MOOB_ONLY` as planned,
although the suite already implements it. Both current-state guides received
dated corrections and now describe the R1/R2 regressions without claiming
that a selected step equals a full-suite pass.

Documentation validation after integration: `sh scripts/check-retired-claims.sh`
reported `RETIRED_CLAIMS_OK`; `python3 scripts/site-faq.py --check` matched
its source; `python3 scripts/check-site.py .` validated 12 HTML pages.
An initial call `python3 scripts/site-journey.py --check` failed with
`KeyError: '0007'` because this generator takes the repository root as its
positional argument, not a `--check` flag. The correct command,
`python3 scripts/site-journey.py .`, completed and left no file changes.

## Limits and next step

The three lower-filesystem runs establish the tested `O_APPEND` transitions,
not every mutable flag or concurrent `F_SETFL` race. Because the change uses
the lower file's `f_lock`, the dedicated ext4 KASAN/lockdep check was run
promptly; the full batch KASAN/lockdep gate is in progress and is not yet
counted as passed. This correction was integrated with R2 before that gate.
