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

## Descriptions for the weak handlers (not yet run)

2026-10-19. Status: descriptions and seeds written and checked locally (below); no fuzzing run
with them yet. Test work only: no change under `layer/`.

Each change and the code it targets (`vm/syzkaller/moobfs.txt`, `vm/syzkaller/seeds/`):

- **`query2_exec`, `cursor_decode`, `first_after`, `last_before` (the `QUERY2` cursor).** The
  cursor carries a MAC keyed per mount, so random or fixed bytes never continue a query. Its 16
  words are now 16 resources: `ioctl$MOOB_IOC_QUERY2` returns them, and the new
  `ioctl$MOOB_IOC_QUERY2_NEXT` (same command, cursor sent) takes words returned earlier and
  returns the next ones; words of different queries mixed give `ESTALE`. Two calls because
  syzkaller's compiler does not let an optional resource be created, and a required one that
  only the same call creates would disable it. Seeds: `query2-pages` (ordered ascending and
  descending pages over equal values, plain-scan pages, a `MISSING_OK` first term that the scan
  swaps out), `long-cursor` (string keys of 100 bytes, longer than the cursor's 96 inline bytes,
  in ordered and prefix scans; then the keys change under a cursor), `cursor-errors` (a cursor
  with the magic byte and a wrong MAC).
- **`value_cmp`, `entry_matches`, `seek_first`, `past_end` (every type and operator).** Terms
  are now a union: a string, int64, time, double or bool term with a valid length and a value
  from a short list, an `EXISTS` term, or the previous any-bytes term for the shapes `term_check`
  refuses. Values written as attributes come from the same lists (shared string prefixes, equal
  integers, double bit patterns for 0, -0, 1, -1, 2, both infinities, NaN, a denormal, the
  largest finite), so files and terms often hold equal or ordered values. Seed `value-types`:
  doubles, times and bools on three files, one name with three types, every comparison
  operator, a NaN term and a NaN attribute (both refused), ordered scans by a double and a time.
- **`leap_collect`, `seek_eq` (the equality plan).** Seed `leap-pages`: two equalities whose
  second run is the shorter (it leads), paged with the cursor; three equalities with a bool; an
  equality on a name the domain lacks; an empty run.
- **`query_run`, `query_repin` (v1 partitions, per-result checks).** Seed `query-checks`: a v1
  `QUERY` over two domains and the part outside them with a limit below the matches; a mode
  change on the domain directory, after which `QUERY2` checks each result, paged.
- **`moobl_xattr_set`, `moobl_xattr_get`.** `security.capability` added to the names (its
  removal takes the mounter's credentials), and `tag`. Seed `xattr-flags`: `XATTR_CREATE` on a
  new and on an existing name, `XATTR_REPLACE` on a missing name, a replacement with another
  type, the reserved name, a bad name, an empty value, writes by path and on the domain
  directory, reads of `user.moob.*` and `security.*`, a read with a short buffer, removals
  (present, absent, capability, reserved, bad), a write after the domain is built.
- `limit` takes 0, 1, 2, 4, 4096 or 4097, so pages are short. The four earlier seeds are
  rewritten in the new syntax (same programs; files opened with `openat$moobfs_file`): syzkaller
  parses seeds non-strictly, and in the old syntax their terms were silently dropped.
- `vm/syzkaller-local.sh` enables `ioctl$MOOB_IOC_QUERY2_NEXT`.

Not reachable from one program: the per-file limit (`ENOSPC` at 64 typed attributes) needs 64
writes, and syzkaller loads no seed longer than 40 calls (`prog.MaxCalls`); the suite covers it.
The `ENOMEM` paths need slab fault injection, which `vm/syzkaller/kernel.config` does not enable.

Checks, on macOS with syzkaller at 8fb0ae89890497b4 and Go 1.26.0 (not on the benchmark host):

- `syz-sysgen` with `moobfs.txt` and `moobfs.txt.const`: exit 0, no error; no new constant.
- A local Go program over the generated target (not in the repository): sizes 1098 (every
  term option), 4440, 4656 (both `QUERY2` variants), 32, 272, cursor 128 at offset 104, terms
  at 264, term value at 70; the manager's call list enabled, `utimensat` excepted (it needs
  `clock_gettime`, unrelated to these descriptions); the ten seeds parse non-strictly, are at
  most 32 calls and encode for the executor; their re-serialization shows the terms and cursor
  links kept; 2 000 generated and five times mutated programs round-trip and encode, with 2 254
  `QUERY2_NEXT` calls and 18 205 cursor words linked to an earlier call.
- The C layout, compiled for x86_64 with `clang -fsyntax-only` and static assertions on the same
  sizes and offsets: pass.
- `go test ./prog/ -short` (generation, mutation, serialization tests over every target): ok.

Next: on the host, `vm/syzkaller-local.sh 60 descriptions` (syz-extract, then the build) and a
six-hour run, measuring the same set of handlers.
