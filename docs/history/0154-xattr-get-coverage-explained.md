# 0154 — moobl_xattr_get at 0 PCs explained by the corpus; pull requests 106 and 107 merged

Date: 2026-10-20. Status: done; this branch awaits CI and the maintainer's approval.

## Merges and the advisory

- Pull request 106 (journals 0151 and 0152, the chmod during the first build of a domain) was
  merged with the maintainer's approval at `c33f8b1`, after its CI and KASAN passed. The private
  advisory GHSA-5vmj-wjqc-7gx6 was published right after, at 13:06 UTC.
- Pull request 107 (the review correction batch with 106 merged into it, journal 0153) was
  merged at `7cc6da8` with the maintainer's approval, after its CI, KASAN included, passed. The
  merge was run by the maintainer: the assistant's merge command was refused by its permission
  classifier. The status lines
  of journals 0152 and 0153 ("pull request open") describe the state when they were written.

## Why moobl_xattr_get was at 0 of 4 PCs

Journal 0145 confirmed the 0/4 in the fifth run's `funccover.csv` and ruled out inlining. It
could not run the seed. Here, the run's own artifacts were read first, without running
anything, and then the seeds were replayed.

**What the artifacts show.**
- `manager.log` (machine check): `getxattr$moob` and `fgetxattr$moob` are in neither list
  of disabled calls, so both were fuzzed.
- `syz-db unpack` of the run's `corpus.db` gives 1 082 programs:

| Call | Programs | Programs that mount the layer |
| --- | --- | --- |
| `fsetxattr$moob` | 149 | 135 |
| `setxattr$moob` | 184 | 137 |
| `getxattr$moob` | 27 | 10 |
| `fgetxattr$moob` | 13 | 4 |

- In most of the 14 read programs that mount the layer, the read cannot reach it. It comes
  before the mount or after an `umount2`, follows a mount with invalid options, has a NULL
  path, or reads `security.selinux`, which SELinux answers without the handler.
- Minimization kept only the `fsetxattr` of the `xattr-flags` seed.
- The descriptions take any `filename` and any `fd_moobfs_file`. Nothing ties a read to a file
  of a mounted layer, so writes, which the seeds have many of, survive far more often than
  reads.

**Replay.** The four seeds were run once each in a disposable guest of the run: the same
kernel (`~/.cache/moobfs-syz/linux-6.12.112`), the same image with `snapshot=on`, and the module
from the image. The command was
`syz-execprog -sandbox=none -procs=1 -repeat=1 -threaded=false -cover -coverfile=cov <seed>`.
The PCs of each call were counted within the range of `moobl_xattr_get`, taken from `nm -S` of
the guest's module plus `/sys/module/moobfs_layer/sections/.text`. Results:

| Seed | Read call | PCs in `moobl_xattr_get` |
| --- | --- | --- |
| `xattr-get-fd` (new) | `fgetxattr` of `user.moob.kind` | 3 |
| `xattr-get-path` (new) | `getxattr` by path | 3 |
| `xattr-get-size` (new) | `getxattr` with size 0 | 3 |
| `xattr-flags` | calls 14, 16, 17 (`user.moob.*`) | 3 each |
| `xattr-flags` | call 15 (`security.selinux`) | 0 |

Every other call of those programs had 0 PCs in the function. The fourth PC is most likely the
`security.*` branch, which runs with the mounter's credentials. Inferred from the source; the
replay did not reach that branch.

**Conclusion.** The function is reachable and is executed under KCOV whenever a program reads
an attribute inside a mounted layer. The 0/4 came from a corpus that kept almost no such read.
It was not a dead function, a disabled call or a missing instrumentation. The S3 criterion
"every reachable function exercised" is met by this replay. The run's report will show it once
a run keeps such reads; the next run checks that.

## Change

Three seeds, `vm/syzkaller/seeds/xattr-get-fd`, `xattr-get-path` and `xattr-get-size`. Each
mounts the layer, sets `user.moob.kind` on a file of it and ends by reading it back. All three
parse with `syz-prog2c` of the host's syzkaller build (`8fb0ae8`). Public surfaces updated:
`README.md` and `README.pt-BR.md` (testing limits), `ROADMAP.md` (S3), `docs/security-roadmap.md`
(S3 update), `site/index.html`, `CHANGELOG.md`. The journey milestone of 2026-10-20 in
`scripts/site-journey.py` stays as written, a record of what was known that day.

## Limitations and next step

- One replay per seed, `-repeat=1`. No fuzzing was run, and the new seeds have not been through
  a fuzzing run yet.
- The replay ran on the benchmark host's KVM guest, not in CI.
- Next: a sixth six-hour run with the new seeds, to see `moobl_xattr_get` covered in the
  manager's report. If reads still do not survive there, add a description that ties the
  read to a file of a mounted layer.
