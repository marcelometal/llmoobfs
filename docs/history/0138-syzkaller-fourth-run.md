# 0138 — syzkaller: a fourth six-hour run with F2 and F4, coverage measured, two gaps fixed

Date: 2026-10-19. Status: run done, no report; the description and harness fixes below are
validated by syzkaller's own build, not yet by a run.

## What ran

- Commit: `main` at f0ea4ec (pull request 93), which carries F2 (ADR-0016, journal 0130) and F4
  (journal 0132). The casefold refusal (issue #95, journal 0136) was merged later and was not
  fuzzed.
- Kernel and descriptions as in journals 0109 and 0133; command on the benchmark host:
  `SYZ=<checkout> GO=<go bin> sh vm/syzkaller-local.sh 21600 module image run`.

## Result

- 6 h (21 600 s), manager exit status 0, no crash directory: no report from KASAN, UBSAN or
  lockdep.
- 625 124 executions (about 29 per second), corpus 1 176 programs, manager coverage 16 239.
- Per-function coverage, from `/funccover?force=1` at 12:08 (two minutes before the end):

| File | Functions reached | PCs |
| --- | --- | --- |
| `layer/attr.c` | 4 of 7 | 50 of 60 (83 %) |
| `layer/index.c` | 59 of 83 | 1 077 of 1 601 (67 %) |
| `layer/main.c` | 60 of 71 | 386 of 643 (60 %) |

- The ioctl and attribute handlers of the S3 threshold, summed over `moobl_root_ioctl`,
  `do_get_info`, `query2_validate`, `query2_target`, `query2_pin`, `query2_one`,
  `query2_exec`, `query_run`, `results_to_user`, `add_candidate`, `term_check`,
  `entry_matches`, `inode_matches_all`, `value_cmp`, `seek_eq`, `seek_first`, `leap_collect`,
  `moobl_xattr_get`, `moobl_xattr_set`, `moob_attr_validate`, `moob_utf8_valid`,
  `moob_attr_name_ok` and `moob_attr_name_reserved`: 585 of 744 PCs (78.6 %), below the 85 %
  of the threshold. Journal 0109 did not list its function set; this list is our
  reconstruction of the threshold's wording, so the two percentages are close but not strictly
  comparable.

## Two gaps found in the harness, fixed

- **`moobl_xattr_get()` was never reached (0 of 5 PCs).** `fgetxattr$moob`, `fsetxattr$moob`
  and `fremovexattr$moob` took a plain `fd`, so the fuzzer gave them whatever descriptor the
  program held, rarely a file of the layer; and there was no `getxattr` by path. Now
  `openat$moobfs_file` returns a new resource `fd_moobfs_file` (still usable wherever an `fd`
  is), the three calls take it, and `getxattr$moob` by path is added and enabled.
- **The coverage of earlier runs was never saved.** The script fetched `/funccover` one minute
  before the end; on a six-hour corpus the page took 8 minutes to answer (measured at 11:02,
  `real 8m9.7s`), so the manager stopped first and the file stayed empty (journal 0133). It
  is now fetched 20 minutes before the end with an 1 100-second limit. This run's file was
  saved because a separate fetch had warmed the page.

Checks: on the benchmark host, `vm/syzkaller-local.sh 60 descriptions` with the new
descriptions (syz-extract, `make descriptions`, `make`): exit 0, no error; `syz-db pack` of the
four seed programs: exit 0. shellcheck of `vm/syzkaller-local.sh`: clean.

## Limitations and next step

- The handlers stay below the threshold; besides `moobl_xattr_get`, the gaps are the `QUERY2`
  cursor (`query2_exec`, 41 of 75), `value_cmp` (12 of 24), `leap_collect` (27 of 43) and the
  error paths of `query_run` and `moobl_xattr_set`.
- Next: a run with these descriptions (and the casefold change), measuring the same set.
