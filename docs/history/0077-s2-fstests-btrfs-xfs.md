# 0077 — S2: fstests over btrfs and XFS (group `quick`)

Date: 2026-10-13. Status: the `quick` group is recorded on the three lower filesystems; the
exclusion list and the `auto` group are not done. Local series (after pull request 57).

## Goal

Complete the first half of security roadmap S2: the same run as journal 0075, with btrfs and XFS
as the lower filesystem, which enables the reflink and dedupe tests that ext4 skips.

## Run

`sh vm/fstests-local.sh ~/fstests btrfs -g quick`, then `xfs`, on the benchmark host, in a
virtme-ng guest of its Debian kernel 6.12.111; fstests v2026.09.22; module of commit 7b42ffe
(with the fixes of journals 0074 and 0076). The two runs were started without the maintainer's
decision that journal 0075 had left open; they are contained in a guest and changed nothing in
the repository.

| | ext4 (journal 0075) | btrfs | XFS |
| --- | --- | --- | --- |
| module of commit | 81348ee | 7b42ffe | 7b42ffe |
| guest time | 95 min | 118 min | 117 min |
| ran | 647 | 647 | 647 |
| passed | 178 | 271 | 298 |
| not run | 451 | 360 | 333 |
| failed | 18 | 16 | 16 |

No kernel diagnostic in either console log (no warning, oops, KASAN or lockdep line; the guest
kernel is the distribution's, without KASAN). Per-test lists:
`vm/fstests/results-btrfs-quick.txt`, `vm/fstests/results-xfs-quick.txt`.

## Findings

- **The reflink and dedupe tests run and pass.** No test is skipped for missing reflink or
  dedupe on either lower. 110 tests that do not run on ext4 pass on btrfs, 120 on XFS.
- **The same 15 tests fail on the three lowers**, all classified in journal 0075: ten by design
  (no execution, no device nodes), one that does not apply (732), four gaps (169 and 420:
  file-attribute calls not passed through; 003 and 120: the layer's `atime` mount options have
  no effect). `generic/503` (the `dax` option) fails on ext4 and XFS and is skipped on btrfs.
- **318 and 688 pass** on both, with the fixed module. They are the two defects of the ext4 run.
- **One new failure, `generic/674` on btrfs.** The test dedupes one file into another, remounts
  the filesystem and expects more free space. The log shows the dedupe done (1 MiB in each of
  the eight cases) and the modes as expected; only the free space is unchanged. Reproduced by
  hand on a scratch btrfs image under the layer: after the dedupe, neither a remount of the layer
  nor `syncfs` on a file of the layer changes the free space; `syncfs` on the lower does. The
  layer has no `sync_fs` operation, so **`syncfs(2)` on the layer returns success without
  syncing the lower filesystem**, and so does the sync of its unmount. That is a durability
  defect of the layer, not of the test; it is fixed and tested in journal 0078.
- On btrfs, 16 tests that pass on ext4 are skipped by fstests because btrfs has no
  collapse-range or insert-range and one `fiemap` option. Everything that passes on ext4 passes
  on XFS.
- The transient "No space left on device" of `generic/215` in the full ext4 run did not happen
  in these two runs. Its cause remains unknown.

## Limitations

- The full ext4 run was not repeated with the current module; 318 and 688 were re-run alone
  there after each fix (journals 0074, 0076).
- One run per lower, on one kernel, without KASAN in the guest.
- The `auto` group and a written exclusion list (the roadmap's "done when") are open.

## Next step

Journal 0078 (the `syncfs` fix). Then the maintainer's decision on the four gaps, and the
exclusion list.
