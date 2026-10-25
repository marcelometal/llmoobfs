# 0197 — First contact and installation on a test host (roadmap C1 and C2)

Date: 2026-10-25. Status: C1 written; C2 written and its guide passed `make install-test` in a
disposable Debian 13 VM. Branch `claudiofp/CONSUMABLE`, published for review with journal 0196. Work started and ended:
2026-10-25, one session; the two VM runs took about 19 minutes each under emulation, plus the
image download. Labor time was not measured. Numbering: as journal 0196, after the evolution
front's 0194 and 0195.

## Goal

The maintainer asked for items C1 and C2 of the consumable path (ADR-0022, journal 0196): a
README that tells a new reader what MoobFS is, for whom and how to try it before its status
history, and a way to install and mount the module on a test host without the test harness.

## C1 — First contact

- Both READMEs opened with a warning box and a paragraph listing pull requests, issues and
  journals since the last tag. That text moved, with its facts unchanged, to `docs/status.md`
  and its courtesy mirror `docs/status.pt-BR.md`; links were adjusted to the new location.
- The READMEs now open with a four-line warning that links the status page, and gain two
  sections after "What MoobFS is": "Is it for you?" (when to try it, when not yet) and "Ways to
  try it" (VM, test host, own program). The link bar gains "Install" and "Status".
- The `## Status` and `## Limitations` sections stay where they were.

Alternative not taken: rewriting the whole README. The rest of it is long but organized by
section; the barrier was the opening.

## C2 — Install and mount outside the test harness

- `scripts/install.sh`, called by new `make install` and `make uninstall` targets: checks the
  running kernel against `scripts/supported-kernels` (only `6.12.111+deb13-amd64`; the upstream
  KASAN kernel is a test build), builds the layer and the tools, copies the module to
  `/lib/modules/<kver>/updates/moobfs/` and the tools and manual pages under `/usr/local`, and
  runs `depmod`. It refuses to replace existing files without `FORCE=1` (AGENTS.md rule for
  creation tools), warns when Secure Boot is on, and `uninstall` refuses while the module is
  loaded. `MOOB_ALLOW_UNSUPPORTED=1` overrides the kernel check with a warning; `DESTDIR` stages.
  `make layer` itself is unchanged, so the build still does not check the version
  (docs/compatibility.md rule 2, dated note added).
- `docs/operations.md`: requirements, the kernel switch, build and install, load, a lower tree
  kept under a root-only directory, manual mount and the option table (from the parser in
  `layer/main.c`), mount at boot by `fstab` and by a systemd mount unit, user directories,
  check, upgrade, backup, uninstall, and a table of failures.
- `make install-test` (`vm/install-test/`): a container with QEMU boots the official Debian 13
  generic cloud image of 2026-10-21 (SHA-512 pinned) with a throwaway overlay, configured by
  cloud-init NoCloud over HTTP, and runs `guest.sh` over SSH. Phases follow the guide's commands
  in its order across two reboots. Nothing is installed or loaded on the host.

No file in `layer/`, `include/` or `common/` changed.

## Finding: a fresh Debian 13 is outside the kernel list

The 2026-10-21 Debian 13 cloud image ships `linux-image-6.12.113+deb13-amd64`. The only listed
kernel is 6.12.111, still installable by exact version from the Debian archive. A user who
installs Debian 13 today therefore has to boot an older kernel, forgoing the fixes of later
ones, before `make install` accepts it. The guide says so. Making a current Debian kernel
supported needs it in CI under the rules of `docs/compatibility.md` §1, which is work for the
core front, not for this path. It is filed as issue 142.

As information only, the forced build (`MOOB_ALLOW_UNSUPPORTED=1`, staged with `DESTDIR`)
compiled against the 6.12.113 headers. The module was not loaded on 6.12.113 and nothing about
that kernel is tested or supported.

## Evidence

`make install-test`, second run, on the working tree of this unit (on top of `4f77890`), in
Docker on an Apple Silicon Mac (amd64 container, QEMU TCG, 4 vCPUs, 4 GiB):

```text
STOCK_KERNEL: 6.12.113+deb13-amd64
UNSUPPORTED_REFUSED: 6.12.113+deb13-amd64
UNSUPPORTED_STAGED_BUILD: compiled on 6.12.113+deb13-amd64, not loaded, not tested
LISTED_KERNEL: 6.12.111+deb13-amd64
INSTALLED: moobfs_layer.ko /usr/local/bin/moobfs-attr /usr/local/bin/moobfs-check /usr/local/bin/moobfs-index
OVERWRITE_REFUSED
LOADED: nodev moobfs_layer
MANUAL_MOUNT: moobfs_layer rw,relatime,lower=/srv/moobfs/lower,nolinks
USERS_ISOLATED: alice reads and tags her note; bob cannot read, list or write
NOLINKS_REFUSED
LOWER_HIDDEN
FSTAB_MOUNT
BOOT_MOUNT: moobfs_layer rw,relatime,lower=/srv/moobfs/lower,nolinks; module 1 loaded
SURVIVED_REBOOT
CHECK_CLEAN
UNIT_MOUNT
UNINSTALL_REFUSED_WHILE_LOADED
UNINSTALLED
LOWER_READABLE: content and user.moob.kind without the module
ELAPSED: 1153 s under TCG emulation
MOOBFS_INSTALL_PASS
```

`BOOT_MOUNT` was taken after the module had been unloaded before the reboot, so the boot loaded
it through the `fs-moobfs_layer` alias. The serial console logs of the three boots contain no
`BUG:`, `Oops` or `WARNING:` line; the guest's `dmesg` was not checked separately.

The first run failed at `SURVIVED_REBOOT`, after every earlier marker had passed: the test
compared `moobfs-attr get` with the bare value, while the tool prints name, type and value as
`list` does. The attribute was present. The comparison was corrected; the same run also showed
that macOS `tar` stored its provenance attribute in every entry of the source archive, now
created with `--no-xattrs`.

Other checks on the final tree: `sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`; the
checker now also covers `docs/status.md`, `docs/status.pt-BR.md` and `docs/operations.md`),
`python3 scripts/check-site.py .`, `sh scripts/check-pins.sh` (`PINS_OK`),
`sh scripts/check-spdx.sh`, shellcheck `--shell=sh` on `scripts/install.sh` and
`vm/install-test/*.sh` (koalaman/shellcheck:stable in Docker) and `git diff --check` exited zero.
`make checkpatch`, `make vm-test` and KASAN were not run: the module did not change.

## Correction before merge: `install.sh` could replace existing entries

While pull request 143 was waiting for merge, a reviewing agent, with the maintainer's
authorization, read `scripts/install.sh`. It found that the promise not to overwrite without
`FORCE=1` did not hold. The merge waited for the correction.

- The existence check was `test -e`, which is false for a dangling symbolic link. So such a link at
  a destination was replaced.
- The copy went to a fixed name, `<destination>.moobfs-new`. If something already existed there
  (for example a symbolic link planted in a staging directory), `cp` wrote through it.
- The last step was `mv -f`, which replaced anything that appeared after the check. With `FORCE=1`
  and a directory at the destination, it moved the file into that directory.
- `uninstall` removed any entry with `test -e`, including a symbolic link to a file elsewhere.

The correction, in `place()`:

- Every entry counts as existing (`-e` or `-L`), and a directory is refused even with `FORCE=1`.
- The copy goes to a new name from `mktemp` in the destination directory, created exclusively and
  removed on any failure.
- Without `FORCE=1`, the copy reaches its destination with `ln -T`. `link(2)` fails on any existing
  entry, so the step cannot replace an entry created after the check. With `FORCE=1` it uses
  `mv -fT`, which renames over a file or link itself and never into a directory.
- `uninstall` removes only regular files and names anything else it leaves.

New test `scripts/test-install.sh` (`make install-unit`, added to the CI job "Tools and
sanitizers"). It sources `install.sh`, runs as an ordinary user in a scratch `DESTDIR`, and covers
these cases: a regular file, a dangling link, a live link, a FIFO and a directory (also with
`FORCE=1`); a link planted at the old temporary name; an entry created between the check and the
final step (a `mktemp` wrapper creates it); `FORCE=1` on a file and on a link; temporary files left
behind; and `uninstall` with a link in place.

Evidence, in the pinned `debian:trixie-slim` container as uid 1000:

- With the corrected script: `INSTALL_UNIT_PASS`.
- Negative round, the same test against the script of commit `de8cf7c` (only the sourcing guard
  added; `fail` made non-fatal to list every case). Every reported defect reproduced: `dangling link: replaced`,
  `directory with FORCE: replaced` (the file ended up inside the directory),
  `planted temporary name: wrote through a link`, `entry created after the check: replaced`,
  `uninstall removed a link`.
- Shellcheck `--shell=sh` is clean. The first push of the correction (`57aad48`) failed the CI
  step `make lint`. The `mktemp` wrapper carried a directive only for SC2329, the code that
  current shellcheck reports. The 0.9.0 of Ubuntu 24.04 reports the same case as SC2317. The
  directive now names both, and `make lint`'s shellcheck command is clean with
  `koalaman/shellcheck:v0.9.0` and `:stable`.
- `make install-test` was run again on the corrected tree: `MOOBFS_INSTALL_PASS` in 1065 s, with
  every marker of the first passing run, including `OVERWRITE_REFUSED`. No `BUG:`, `Oops` or
  `WARNING:` line appears in the three console logs.

## Public surfaces

Updated: both READMEs, the new status pages, `ROADMAP.md`, `docs/README.md`,
`docs/public-surfaces.md`, `docs/testing.md`, `docs/compatibility.md`, a dated note in
`docs/distribution-strategy.md`, `CHANGELOG.md`, `docs/references.md`. ADR-0022's decision 4 was
reworded before publication so that a from-source guide for a dedicated test host is not read as
the installation announcement it keeps behind stage 2.

The maintainer approved publication the same day. The site's guide gained section 5, "Install on a
dedicated test host", and the landing page links it. Several places said never to load the
modules "on your host", and `docs/testing.md` called the VM "the only supported way to run the
module". That contradicted the new guide. `CONTRIBUTING.md`, both READMEs, `docs/testing.md` and
the site now say never to load them on a machine used for anything else, and point to
`docs/operations.md`. The old phrases are in `docs/retired-claims.txt`. The status pages still say
"tested in a VM only", which remains true. The GitHub description and discussions state nothing
this unit changes.

## Limitations

- One run of the full guide in an emulated VM, from one cloud image. Not run on real hardware,
  with Secure Boot on, on another distribution, or with btrfs or XFS as the lower (the image's
  root is ext4).
- The roadmap's definition of done (someone who did not write the documents, within 15 minutes
  of their own time) is not met: the test is a script by the author, and queries from a program
  as two users wait for C4 and C5.
- `make install` builds as root inside the clone. The guide accepts that for a dedicated test
  host.

## Next step

The pull request with journals 0196 and 0197 waits for its checks on the correction above and the
maintainer's merge.
Then C3 (container recipe) or C4 (`moobfs` CLI). The core front owns issue 142, which asks to add
the current Debian 6.12 kernel to CI and to `scripts/supported-kernels`.
