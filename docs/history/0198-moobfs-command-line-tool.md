# 0198 — The `moobfs` command-line tool (roadmap C4)

Date: 2026-10-25. Status: written; the VM suite and the install test passed (evidence below).
Branch `claudiofp/CONSUMABLE-C4`. Work started and ended: 2026-10-25, one session, after PR 143
merged; calendar time about four hours including the VM runs under emulation. Labor time was not
measured. Estimated effort in the roadmap: M.

Numbering: the evolution front's G5 branch holds a local journal numbered 0196, which collides
with the 0196 already on `main`. It will be renumbered at merge, as ADR-0022 expects. This one
takes 0198, the next free number on `main`.

## Goal

Item C4 of the consumable path (ADR-0022): one command to query a domain, see a mount's state
and check a tree. Until now, a query from the shell needed the test client `tests/query.c` or
code that issues the ioctl. User space only; no new ioctl.

## Decisions

- **One binary, `moobfs`, with subcommands `info`, `query` and `check`**, in C next to the other
  tools (`tools/moobfs.c`), built by `make attr-tools` and installed by `make install`. It
  shares the term parser `tools/query-common.h` with `moobfs-index` and the test client, so the
  syntax of terms is the same everywhere (`name op type value`). Alternative not taken: a Python
  CLI. That needs an interpreter on the host and overlaps the client library of C5, which
  builds on G1.
- **`query` chooses the ioctl.** `QUERY2` on a domain directory, following the cursor until the
  answer is complete or `--limit` is reached. The mount-wide `QUERY` on the root of a mount
  without domains, which has no cursor: more than 4096 results exit 3 with the ones it got.
  `ESTALE` (the domain changed how it answers between pages) restarts the answer, at most three
  times. `EAGAIN` is retried three times, as spec-query-v2 allows. Without `--order` the results
  are sorted by inode number, so the output equals `moobfs-index`'s.
- **`info`** finds the mount through `/proc/self/mountinfo` (device and longest mount-point
  prefix) and issues `GET_INFO` on its root, because the kernel answers it only there. Through
  a bind mount of a subdirectory the root is not reachable, and `info` says so. That is the open
  point of the client contract, section 7, not something this item changes. The mount options
  `index_build_work` and `index_idle` come from mountinfo, where the kernel shows them when they
  differ from the default.
- **Every error is a sentence and an exit status** that a script can test. The codes follow
  sysexits(3): 64 usage, 65 invalid terms or `EINVAL`, 69 nothing to query there (module not
  loaded, another filesystem, not a domain, `noindex`), 75 temporary (`ENOMEM`, `EDQUOT`,
  `E2BIG`, `ETIMEDOUT`, `EAGAIN`), 77 permission. The table is in `docs/tools.md`.
- **`check`** runs `moobfs-check` from the same directory, else from `PATH`.
- **Tests as two users** need a way to run a program under another uid in the VM guest, which has
  no `runuser`. `tests/asuid.c` does `setgroups`, `setgid`, `setuid` and `exec`. It
  is built by `vm/build-tests.sh` like every test program.

## Changes

- `tools/moobfs.c`, `man/moobfs.1`, the `Makefile` target, `scripts/install.sh` (installs and
  removes `moobfs`), `vm/in-container.sh` (stages it; the KASAN, KCSAN and arm64 jobs build
  `make attr-tools` into the staged directory already).
- `vm/suite.sh`: the CLI block after the `QUERY2` isolation checks, once per lower filesystem
  (`CLI_PASS`); a mount-wide check and a `noindex` check in the `domain_depth=0` part; an
  `E2BIG` check at the end of the ext4 part (`CLI_E2BIG_PASS`); "module not loaded" after the
  final `rmmod`.
- `vm/install-test/guest.sh`: alice finds her note with `moobfs query`, bob is refused with exit
  77, and `moobfs info` describes the mount (`CLI_QUERY`).
- Documentation: `docs/tools.md` (synopsis, behaviour, exit table), `docs/operations.md`
  section 5, `docs/testing.md`, both READMEs ("Ways to try it"), the site guide, `ROADMAP.md`
  (C1 and C2 marked done after PR 143, which the roadmap still called unpublished; C4 status).

## Evidence

The first ext4 run stopped at `cli-eacces-message`: when `open(2)` itself is refused, the tool
printed `strerror`'s "Permission denied", and the test looks for the tool's own wording. Every
comparison with the oracle before that step had passed. The permission case now goes through the
same message as an `EACCES` from the kernel.

The second ext4 run passed the CLI block and then failed a later, unrelated check,
`q2-fallback`. The first version of the `E2BIG` check created its directory through the layer
and ran `chmod` on it. A directory permission change inside a domain switches that domain to
per-result checks, and the suite later counts those domains (`fallback_domains=1`). The check
moved to the end of the ext4 part, where the layer is unmounted while the tree is written.

Third run, `MOOB_ONLY=ext4 make vm-test`:

```text
CLI_PASS: ext4; two users equal to the oracle, paging, order, limit, JSON, info, check and explicit errors
CLI_E2BIG_PASS
LAYER_LOWER_PASS: ext4 reflink=no
MOOBFS_ONLY_PASS: steps ext4 (not a complete run)
```

Full suite, `make vm-test` on the final tree, in Docker on an Apple Silicon Mac (QEMU TCG):

```text
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
CLI_PASS: ext4; two users equal to the oracle, paging, order, limit, JSON, info, check and explicit errors
CLI_E2BIG_PASS
CLI_PASS: btrfs; two users equal to the oracle, paging, order, limit, JSON, info, check and explicit errors
CLI_PASS: xfs; two users equal to the oracle, paging, order, limit, JSON, info, check and explicit errors
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; ...
```

`make install-test` on the final tree: `MOOBFS_INSTALL_PASS` in 1257 s, with every marker of
journal 0197 and the new `CLI_QUERY: alice finds her note through the index; bob is refused with
exit 77`. No `BUG:`, `Oops` or `WARNING:` line appears in its console logs.

KASAN was not run locally. The module did not change; the KASAN job of the pull request runs the
same suite, with the new steps, under the sanitizers.

Other checks: gcc 14 and clang 18 with the project's `WARN` flags; `mandoc -Tlint -Wwarning` on
every manual page; shellcheck 0.9.0 and stable; `check-retired-claims`, `check-site`,
`check-pins`, `check-spdx`, `git diff --check`.

## Limitations

- `ENOMEM`, `EDQUOT`, `ETIMEDOUT` and `EAGAIN` print their messages by code; the VM suite does
  not provoke them (the kernel's own tests do). `E2BIG` is the temporary refusal that was run.
- Paging was compared on small answers (one result per call). The cursor under concurrent
  writes is the kernel's `race2` test, not this tool's.
- No `QUERY2_BATCH` (OR of up to eight conjunctions). A user who needs it uses the SQLite
  virtual table or the client contract.
- `info` cannot work through a bind mount of a subdirectory (client contract, section 7).
- The definition of done of the consumable path still needs someone who did not write the guide,
  and the library of C5.

## Next step

Review and publish C4 as one pull request. Then C3 (container recipe), which can use
`moobfs query` inside the container to show what a bind mount allows.
