# 0091 — S2: the `quick` group passes on the three lower filesystems with the exclusion list

Date: 2026-10-13. Status: done for the `quick` group; the `auto` group is not run. Local series
(after pull request 63).

## Goal

The "done when" of security roadmap S2: an exclusion list justified test by test and a
reproducible result. Journal 0084 left the complete run with the list to be made.

## Run

`sh vm/fstests-local.sh ~/fstests <fs> -g quick -E vm/fstests/exclude-quick.txt`, for ext4, btrfs
and XFS in turn, on the benchmark host in a virtme-ng guest of its Debian kernel 6.12.111;
fstests v2026.09.22; module of commit `db8f75b`, which has every fix up to journal 0084 (the
same layer code as `v0.3.0-experimental`).

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 631 | 631 | 631 |
| passed | 186 | 280 | 307 |
| not run | 445 | 351 | 324 |
| failed | 0 | 0 | 0 |

`./check` printed "Passed all 631 tests" on each. No kernel diagnostic in any console log.

## Reading

- The 16 excluded tests are the whole difference from a clean run: ten by design (no execution,
  no device nodes), two for the `atime` limitation, three that do not apply, and one
  (`generic/607`) listed without having been investigated.
- Against the first runs (journals 0075 and 0077) the passed counts rose by 8, 9 and 9: the
  file-attribute tests that used to be skipped now run (journal 0079).
- `generic/748` takes 20 to 29 minutes of each run by itself (5 000 iterations), which is most
  of what makes "quick" two hours here.

## Limitations

- One run per lower, one kernel, no KASAN in the guest.
- A test that is not run proves nothing: 324 to 445 of the 631 are skipped, mostly for features
  a stacked filesystem without a block device cannot offer.
- The `auto` group, which is several times larger, was not run.

## Next step

The M4 measurements now running on the same host; then the `auto` group as a background job.
