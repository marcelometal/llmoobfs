# 0205 — Debian 6.12.113 supported: fstests quick passed on three lowers (issue 142, phase 2)

Date: 2026-10-26 to 2026-10-27. Status: done on a local branch; issue 142 can close once merged.
Work started: 2026-10-26 16:10 America/Sao_Paulo, after the maintainer said to go on with phase
2. Ended: 2026-10-27 05:36. Wall-clock time, not an engineering-effort
measurement; most of it was the three fstests runs (about 5.7 h) and the night. Phase 2 was
estimated M; the work itself took a few hours.

## Goal and decision

A Debian 13 system installed from the 2026-10-21 cloud image runs `6.12.113+deb13-amd64`, which
`make install` refused (issue 142, journal 0197). Journal 0203 put that kernel in CI (the full
suite passes on it in the job `Native Linux VM (Debian 6.12.113)`) and left rule 3 of
compatibility.md §1 to meet: one fstests `quick` run per lower filesystem. This unit records those
runs and adds the kernel to `scripts/supported-kernels`.

The criterion is the one of journal 0091 for 6.12.111: `./check -g quick` with
`vm/fstests/exclude-quick.txt`, expected to end with no failure on ext4, btrfs and XFS, and no
kernel diagnostic.

## Alternatives

- **Reboot the dedicated host into 6.12.113** and run `vm/fstests-local.sh` unchanged. Not
  chosen: the host is the benchmark machine, its runs since journal 0042 are on 6.12.111, and a
  power cut would have left it on the other kernel. The guest boots 6.12.113 instead; the host
  keeps 6.12.111.
- **The fstests runs in CI.** Not chosen: about two hours per lower under emulation, three
  times the job limit of the free runners.

## Changes

- `vm/fstests-local.sh`: `MOOB_KVER` names an installed kernel (image in `/boot`, modules and
  headers under `/lib/modules`); the layer is built against its headers and `vng -r` boots its
  image. Without it, the running kernel as before. `vm/fstests-guest.sh` prints the guest's
  kernel before `./check`.
- `scripts/supported-kernels`: `6.12.113+deb13-amd64` added; the comment names rule 3.
- `vm/install-test/guest.sh`: the image's stock kernel is now the installation target. Phase 0
  checks that it is listed and that `make install` refuses an unlisted release (a `KVER`
  override, since the image has no unlisted kernel any more), and no longer switches to
  6.12.111. The final marker says so. `vm/container-test/guest.sh` still switches to 6.12.111,
  which stays supported; it was not changed.
- `vm/fstests/README.md`: the `MOOB_KVER` step and the 6.12.113 result; per-test lists
  `vm/fstests/results-{ext4,btrfs,xfs}-quick-6.12.113.txt`.
- Current-state documents: both READMEs, `SECURITY.md`, `docs/faq.md` and `site/faq.html`,
  `docs/operations.md` §1 (no kernel switch on a fresh install; the switch kept for a host that
  updated past the list) and "What was tested", `docs/compatibility.md` §1, `docs/testing.md`,
  `docs/distribution-strategy.md`, the changelog and the history index.

## What ran

On the dedicated Debian host (x86-64, KVM, host kernel 6.12.111): Debian's
`linux-image-6.12.113+deb13-amd64` and `linux-headers-6.12.113+deb13-amd64` at `6.12.113-1` from
`trixie-security`, virtme-ng 1.41 from `vm/kasan-requirements.txt` by hash, fstests v2026.09.22
already built there. The host's GRUB default was pinned to 6.12.111 (`GRUB_DEFAULT="1>2"`, the
previous file kept as `/etc/default/grub.bak-k113`), since installing the newer kernel would
otherwise have made it the next boot.

A two-test smoke run (`generic/001`, `generic/013` on ext4) passed and fstests' `check.log`
recorded `Kernel version: 6.12.113+deb13-amd64`. Then, one lower after another:

```text
MOOB_KVER=6.12.113+deb13-amd64 sh vm/fstests-local.sh ~/fstests <lower> -g quick -E vm/fstests/exclude-quick.txt
```

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| ran | 631 | 631 | 631 |
| passed | 187 | 281 | 308 |
| not run | 444 | 350 | 323 |
| failed | 0 | 0 | 0 |
| wall time, build included | 101 min | 120 min | 121 min |

Each printed "Passed all 631 tests", `MOOBFS_FSTESTS_CHECK_EXIT: 0` and no
`MOOBFS_FSTESTS_KERNEL_DIAGNOSTIC`; `check.log` names 6.12.113 in each. The 6.12.111 run of
journal 0091 passed 186, 280 and 307: one test more passes on each lower here and one fewer is
skipped. That run kept no per-test list, so the test is not identified.

`make install-test` with the new phase 0, on the same host with `MOOB_HOST_QEMU=1` (KVM): the
image booted `6.12.113+deb13-amd64`, `UNSUPPORTED_REFUSED: 6.12.999+deb13-amd64 (KVER override)`,
`LISTED_KERNEL: 6.12.113+deb13-amd64`, then every step through `UNINSTALLED`;
`MOOBFS_INSTALL_PASS` in 397 s.

## Errors found on the way

- Updating this branch, a compound shell command ran `git stash pop` after a fast-forward and
  applied an unrelated stash from another session ("issue5-old-base", the stash list is shared by
  every worktree). It conflicted, so git kept the stash; the two touched files were restored from
  `HEAD`, and the stash is still in the list, untouched.
- The fstests summary printed by `vm/fstests-local.sh` cuts each list at 2000 characters; the
  counts above come from the full console logs.

## Limitations

- The fstests runs were made once per lower, by hand, on one host, as for 6.12.111; they are not
  in CI. The KASAN job stays on upstream 6.12.112.
- A Debian system that updates past 6.12.113 leaves the list again; the guide says how to boot a
  listed kernel, and rule 3 is the way to add the next one.
- The `auto` group of fstests was not run on either kernel.

## Public surfaces

Every current-state document that named the supported kernels or the refusal of 6.12.113 was
updated (list above). `site/guide.html` and `site/index.html` describe `make vm-test` and the
pilot numbers, both still on 6.12.111, and were left as they were. Issue 142 closes with this
change; its comment links this journal.

## Next step

Close issue 142 after the merge. The next kernel follows rule 3 the same way: the CI job of
journal 0203, then these three runs with `MOOB_KVER`.

## Checks of this commit

Completed 2026-10-27 at 05:36 America/Sao_Paulo. `git diff --check`, `sh scripts/check-spdx.sh`,
`sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`, two phrases retired),
`python3 scripts/check-site.py .` (12 pages), `python3 scripts/site-faq.py --check` (the FAQ page
regenerated) and `shellcheck` 0.9.0 on every `vm/*.sh` and `scripts/*.sh` passed. A local check
resolved 630 relative Markdown destinations, anchors included, in the twelve changed documents.
No kernel code changed.
