# 0112 — A lower dentry pinned by a failed mount (syzkaller); ENOMEM reported as EINVAL; a SQL condition dropped on an older SQLite

Date: 2026-10-14. Started 18:30, ended 20:10 (calendar time, local). Status: fixed; evidence
below. Estimated effort: S.

## A failed mount kept a reference on the lower root (found by syzkaller)

The six-hour syzkaller run of journal 0109 reported `BUG: Dentry ... {n=file0} still in use (1)
[unmount of tmpfs tmpfs]` when the executor's tmpfs was unmounted; `file0` was the directory a
program had used as the layer's `lower=`. The log shows syzkaller's fault injection failing
allocations inside `moobl_fill_super()` just before.

Cause, read in `layer/main.c`: the mount passes `dget(sbi->lower.dentry)` to
`moobl_set_lower_path()`, which takes the reference only when its allocation succeeds. On failure
it returned the error and the mount returned it too, so the reference was never dropped. The
other caller, `moobl_lookup()`, drops it on that path; only the mount did not. The lower
filesystem then cannot be unmounted cleanly: a reference leak of the kind that can end in a
use-after-free. Before this, the fault-injection test of journal 0096 (`tests/failnth.c`) covered
18 operations but not the mount, and its kernel-log check ran before the lower was unmounted, so
it could not see a pinned lower.

- **Fix:** the mount drops the reference when `moobl_set_lower_path()` fails.
- **Test:** `tests/failnth.c` gains the mount (a second mount of the layer over the same lower,
  each allocation of the mount failed in turn); `vm/kasan-guest.sh` checks the kernel log again
  after the lower is unmounted.

## Out of memory at mount reported as an invalid argument (found by the new mount test)

The first runs of the new mount operation failed both with and without the fix above: the
third allocation of the mount, failed on purpose, gave `EINVAL`. `kern_path()` on `lower=`
returned `-ENOMEM`, and `moobl_fill_super()` turned any of its errors into `EINVAL` ("cannot
resolve lower directory"); the reading of the format marker did the same. A caller was told its
path was wrong when memory had run out, the class of error journal 0096 found in the ioctls.

- **Fix:** both report the error they got, with the same message in the mount's log.

## A SQL condition could be dropped and declared handled (KASAN job of pull request 71)

The KASAN workflow ran the suite step `sqlext` (journal 0108) on the runner's SQLite, older than
the 3.46 of the local VM, and `SELECT path FROM vt WHERE score = 5.0` returned 119 rows instead of
6. The extension declared a condition handled (`omit`) when the constant seen while planning
(`sqlite3_vtab_rhs_value()`) could be represented exactly; that SQLite handed `5` (the INTEGER
column's affinity applied) while planning and `5.0` to `xFilter`, which dropped the condition as
not exactly representable. Declared handled and dropped, the condition reached nobody.

- **Fix:** only `IS NOT NULL` is declared handled; SQLite checks every other pushed condition again
  on each row. An equality's column is answered from the request (journal 0108), so that check
  reads no file; ranges cost a `getxattr` per row in the check, until projection (ADR-0014).

## Results

`MOOB_ONLY=faults sh vm/kasan-local.sh` on the benchmark host (upstream 6.12.112, KASAN, UBSAN,
lockdep, fault injection confined to the module):

- with every fix: `MOOBFS_KASAN_ONLY_PASS`; `FAILNTH mount faults=9 failed_cleanly=9`;
  `FAILNTH_DONE: operations=19 calls=302 faults=283 unexpected=0`; no kernel diagnostic, the lower
  unmounted cleanly.
- negative run, the reference not dropped (the EINVAL fix kept): fails at
  `fi-lower-pinned after the layer was unmounted` with `BUG: Dentry ... {n=fi} still in use (1)
  [unmount of ext4 vda]`, the report syzkaller made, now reproduced without the fuzzer.
- `sh scripts/checkpatch.sh`: exit 0; shellcheck at the CI's severity: clean.
- The SQL fix: the suite step `sqlext` in the KASAN workflow of the pull request (the run that
  showed the defect); not reproducible on the local SQLite 3.46, where both moments see `5.0`.

## Limitations

- The fault test mounts with one set of options; syzkaller's mounts vary them.
- syzkaller had not finished reproducing its crash when the cause was read in the code; its
  reproducer, if it completes, is to be kept with the tests (S3).

## Next step

The KASAN workflow of pull request 71 with these fixes; the six-hour syzkaller run's end.
