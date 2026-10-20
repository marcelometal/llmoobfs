# 0141 — Typed attribute limits and query validation tested case by case

Date: 2026-10-19. Status: tests added; they pass on ext4, btrfs and XFS in a local arm64 guest without KASAN; the KASAN job
of the pull request has not run them yet. Branch `typed-limits-tests`. Test work only: `layer/`
untouched. Estimated effort: S.

## Goal

Journal 0138 lists what syzkaller reaches poorly: the per-file limit of typed attributes
(`ENOSPC` at `MOOB_ATTR_MAX_PER_FILE` = 64 needs 64 writes before the one that fails, which a
seed does not do), the error paths of `moobl_xattr_set()` and `moobl_typed_check()`
(`layer/main.c`), and the request checks of `term_check()`, `order_check()`,
`query2_validate()` and `do_query()` (`layer/index.c`). The suite already covered part of it
(`tests/layer.c`: one 65th attribute, a replacement at the limit, five invalid encodings, an
uppercase name and the reserved name; `tests/query.c invalid` and `q2invalid`: the header fields
and some term checks). This entry adds what was missing, each rule of docs/spec-m2.md §3 and §8
and docs/client-contract.md §4 with its own case.

## What is added

- `tests/layer.c typed <mount> <lower>`, marker `TYPED_LIMITS_PASS`, run by `vm/suite.sh` once
  per lower filesystem after the pass-through tests (`fail typed-limits-<fs>`). Each refusal is
  checked twice: the errno, and a snapshot of every extended attribute of the lower file (names
  and values) equal before and after.
  - Bounds accepted, so that the refusals are for the reason named: a 1 024-byte string, a raw
    value, int64, time, double +infinity, bool false, a string with two-, three- and four-byte
    UTF-8 up to U+10FFFF, a 64-byte name, a name using every character class.
  - Names: `user.moob.format` gives `EPERM` (also with `XATTR_CREATE`); empty, uppercase,
    non-ASCII, 65-byte, leading-dot, trailing-dot, doubled-dot and out-of-alphabet (`-`, space)
    names give `EINVAL`; a removal with an invalid name gives `EINVAL`.
  - Encodings, on an absent well-formed name: empty value; type bytes 0, 7 and 0xff; int64,
    time and double of 7 and 9 bytes and int64 of 0; quiet, signalling and negative NaN; bool of
    0 and 2 bytes and values 2 and 0xff; strings with NUL, a lone continuation byte, overlong
    two-, three- and four-byte forms, a surrogate, code points above U+10FFFF, a sequence cut
    by the end and a bad second byte; a 1 025-byte string; a 4 097-byte raw. All `EINVAL`, and
    an invalid replacement keeps the value already there (also with `XATTR_REPLACE`).
  - Flags: `XATTR_CREATE` on an existing typed name gives `EEXIST`; `XATTR_REPLACE` and removal
    on an absent one give `ENODATA`.
  - Limit: with `user.moob.format` planted on the lower file and the opaque names `user.other.a`,
    `user.moob` and `user.moobx.k` (the last two hold bytes that would be invalid typed values,
    so they are not validated either), 64 typed attributes are accepted and the 65th distinct
    one gives `ENOSPC`, also with `XATTR_CREATE`. An opaque attribute of the same size is then
    accepted, so the `ENOSPC` is the layer's limit, not the lower's xattr space. At the limit a
    replacement succeeds (with and without `XATTR_REPLACE`), `XATTR_CREATE` of an existing name
    gives `EEXIST`, an invalid value still gives `EINVAL`, a removal frees one slot (the new name
    is accepted, the next gives `ENOSPC`), and `listxattr` through the layer shows 64 typed names.
- `tests/query.c q2invalid` (marker unchanged, `QUERY2_INVALID_REQUESTS_REFUSED`): each refusal
  next to a valid request (`Q2OK`) that differs by one field. `EXISTS` with a type or a length;
  operators 0 and 8; an empty name, a name of 65 bytes without NUL (a 64-byte name accepted),
  `a..b`, `.kind`, `kind.`, `a-b`; term pad bytes 1 and 2; header `pad[0]` and `reserved[0]`;
  an invalid second term (name, operator) and an invalid fourth term; raw, 0 and 7 as query
  types; int64 of 7 and 9 bytes, time of 0 bytes, `PREFIX` on time, double and bool; NaN and
  negative signalling NaN (+infinity accepted); double of 4 bytes; bool 2, of 0 and 2 bytes; a
  string `len` of 1 025 and of 0xffff (1 024 accepted, the empty string accepted); strings with
  NUL, overlong, surrogate, above U+10FFFF, cut, lone continuation; an "or missing" term whose
  bool value is 2 (the same term with 1 accepted); order names `a..b` and `created.`, order type
  7; `order_by` alone with string, int64 and double accepted; limit 0 with `order_by`, limit
  0xffffffff.
- `tests/query.c invalid` (marker unchanged, `QUERY_INVALID_REQUESTS_REFUSED`): v1 `QUERY` with
  zero terms, a limit of 4 097, `reserved[0]`, `MOOB_TERM_MISSING_OK` (a `QUERY2` flag) and an
  invalid second term; the valid two-term request afterwards.
- `docs/testing.md`: the new marker, and the two `query` rows extended.

## Validation

- Compiled in a `debian:13` container with the flags of `vm/build-tests.sh` (`-static -O2 -Wall
  -Wextra -Werror`): `tests/layer.c`, and `tests/query.c` with `common/attr.c`. Clean.
- `shellcheck --shell=sh vm/suite.sh` (koalaman/shellcheck:stable): clean.
- Run locally, not the whole suite: an arm64 `debian:13` container on an Apple-silicon host, the
  Debian kernel 6.12.113+deb13-arm64 (no KASAN) under virtme-ng 1.35 and QEMU TCG, the module
  built against its headers, three blank 320 MiB disks made ext4 (`-b 4096`), btrfs and XFS in
  the guest. On each: `layer typed` over a `domain_depth=0` mount, `query invalid` on its root,
  then `query q2invalid` on a domain directory of a default mount. Result: `TYPED_LIMITS_PASS`,
  `QUERY_INVALID_REQUESTS_REFUSED` and `QUERY2_INVALID_REQUESTS_REFUSED` on ext4, btrfs and XFS;
  nothing matching `BUG`, `Oops`, `WARNING:`, `KASAN`, `UBSAN` or `circular` in `dmesg`. (The
  guest script's attempt to put a typed attribute in the domain first used the wrong arguments
  and printed "query: invalid terms"; `q2invalid` ran on an empty domain, as its valid cases
  allow.) The guest script is not part of the repository.
- Not run: `vm/suite.sh` as a whole, and anything under KASAN, UBSAN or lockdep. The KASAN job of
  the pull request (x86-64, upstream 6.12.112) runs the new step within `run_lower`.

## Observations

- At the limit, `XATTR_REPLACE` on an absent typed name gives `ENOSPC` rather than the lower's
  `ENODATA`: `moobl_typed_check()` counts before the lower sees the flag. Neither the spec nor the
  client contract says which errno wins; read from the code, not asserted by the test. Not a
  discrepancy with a written rule; recorded so that a later choice is explicit.
- No test contradicted docs/client-contract.md §4 or docs/spec-m2.md §3 and §8.

## Limitations

- ENOMEM paths (allocation failures in `moobl_count_typed()`, `do_query()`, `query2_one()`) are
  not reached by these tests; they need fault injection (the `faults` step of
  `vm/kasan-guest.sh` covers some).
- The lowers of the suite (ext4 with 4 KiB blocks, btrfs, XFS) all hold 64 small entries; a lower
  whose xattr space is smaller (ext4 with 1 KiB blocks, spec-m2 §3) is not exercised here.
- The query cases check `EINVAL` only, not that a refused request leaves the output fields
  untouched.

## Next step

Push `typed-limits-tests`; the KASAN job of its pull request runs the suite with the new step on
x86-64.
