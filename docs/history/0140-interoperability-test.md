# 0140 — Interoperability test: xattr tools through the layer

Date: 2026-10-19. Status: implemented; passed in one local arm64 run, **not run in CI yet**.

## Goal

Show what common xattr-aware tools do through a layer mount, and turn the facts that do not
depend on a tool's presentation into a test with a verdict. Tools: getfattr/setfattr (attr),
fselect, cshatag, Recoll and KDE KFileMetaData (the library Dolphin and Baloo use for tags,
rating and comment).

## Decisions

- On demand only (`workflow_dispatch`), never a required check. No weekly schedule: every tool
  is pinned, so a weekly run would repeat the same versions; it is meant to be dispatched after
  a change to the layer's xattr handling or to a pinned version. GitHub dispatches only
  workflows present on the default branch, so it can first run after merge.
- A plain (non-debug) build of the kernel version pinned by `kasan.yml`, amd64, KVM through the
  same udev rule; one lower filesystem, ext4.
- Checked: what the contract fixes (refusal of a plain value under `user.moob.*`, identical bytes
  through the layer and below, pass-through of other `user.*` names) and what each tool's own
  function needs (cshatag's detection, Recoll's field search, fselect's `has_xattr`). Recorded,
  not checked: how a tool presents a typed value (the type byte, raw integers), which is the
  tool's choice and documented in `docs/client-contract.md` §4.
- fselect, cshatag and Recoll are required (missing one is `INTEROP_FAIL: setup (...)`); the
  KFileMetaData helper is optional (`INTEROP_SKIP: kfmd`), so a KDE packaging problem does not
  hide the other results.
- Pinned versions: fselect 0.10.3 and cshatag v2.2.1 release assets by SHA-256; Recoll
  (`recollcmd 1.36.1-1build2`) and KFileMetaData (`libkf5filemetadata-dev 5.115.0-0ubuntu4`) by
  their ubuntu-24.04 release-pocket versions; attr is the runner's and its version is printed.
  fselect's digest is the one GitHub lists for the asset; cshatag publishes only a GPG
  signature and GitHub lists no digest, so its SHA-256 was computed from the official amd64
  tarball on 2026-10-19 (both digests were recomputed that day from fresh downloads and matched).

## Changes

- `vm/interop-guest.sh`: the guest script. Checks print `INTEROP_OK: <step>` or
  `INTEROP_CHECK_FAIL: <step>`, observations `INTEROP_OBSERVED:`, and the last line is
  `INTEROP_PASS` or `INTEROP_FAIL: <steps>`. Steps: `hex-layer-equals-lower`,
  `einval-plain-moob`, `hex-typed-write`, `xdg-tags-roundtrip`, `fselect-has-xattr`,
  `fselect-xdg-value`, `cshatag-ok`, `cshatag-outdated`, `cshatag-corrupt`,
  `cshatag-keeps-typed`, `recoll-moob-field`, `recoll-xdg-fields`,
  `hex-layer-equals-lower-final`, `kernel-log` (`vm/kdiag.sh`).
- `vm/interop-kfmd.cpp`: a small KF5 program calling `KFileMetaData::UserMetaData`.
- `vm/interop.config`: the kernel configuration fragment (ext4, modules, virtio-blk, no
  sanitizers).
- `.github/workflows/interop.yml`: builds the kernel, the module and `moobfs-attr`, installs the
  tools, runs the guest, passes only on a line `INTEROP_PASS`, and uploads `console.log` and
  `dmesg.txt` as `interop-evidence`.
- `scripts/check-pins.sh`: `interop.yml`'s kernel pin must equal `kasan.yml`'s; the tool pins
  are printed. `docs/development.md`, "Pinned versions": the new copies.
- `docs/client-contract.md` §4, `docs/testing.md`, `docs/references.md`, `CHANGELOG.md`.
- The script is under `vm/`, so `make lint` (shellcheck over `git ls-files 'vm/*.sh'`) and
  `scripts/check-spdx.sh` cover it without a list to update; `kasan.yml`'s path filter already
  includes `vm/`, so this pull request runs the KASAN suite too.

## Results (local arm64 run, not CI)

Docker `debian:trixie` (arm64) on Apple Silicon, QEMU with TCG, virtme-ng 1.35 booting Debian's
`6.12.113+deb13-arm64` kernel, module built against its headers; attr 2.5.2, fselect 0.10.3
(aarch64 musl asset), cshatag v2.2.1 built from the tag (no arm64 release asset), Recoll 1.43.0,
KFileMetaData 5.116.0. All fourteen steps `INTEROP_OK`, final `INTEROP_PASS`, no kernel
diagnostic. Observations:

- getfattr hex: `user.moob.kind=0x016465636973696f6e`, `user.moob.score=0x022a00000000000000`,
  identical through the layer and on the lower; getfattr's automatic text encoding printed one
  string value with the 0x01 invisible and another as base64 (`0sAW5vdGU=`).
- fselect `xattr(user.moob.kind)` returns bytes `01 64 65 ...` (`\x01decision`) and
  `xattr(user.moob.score)` the 9 raw bytes; `= 'decision'` matches nothing, `like '%decision'`
  matches.
- Recoll `moobkind:decision` and `moobkind:note` find their files (0x01 separates terms); the
  stored field keeps the type byte; `moobscore:42` finds nothing.
- KFileMetaData `attribute(moob.kind)=[\x01decision]`; tags, rating and comment it writes land on
  the lower as plain `user.xdg.tags`, `user.baloo.rating`, `user.xdg.comment`.

An earlier report-only run of the same tools (same environment, before the checks existed) had
the same observations.

Error found and fixed while making the checks: the first local run of the test failed
`cshatag-corrupt` (cshatag said `<outdated>`). The reference file for `touch -r` was in the
guest's `/tmp`, which in that run was the container's root shared into the guest, and it kept
only whole seconds; the restored mtime lost its fraction and cshatag, which compares to the
nanosecond, saw an ordinary edit. The reference file is now on the lower ext4.

## Limitations

- Never run in GitHub Actions: the amd64 package names and versions were checked with
  `apt-cache policy` in an `ubuntu:24.04` amd64 container, not on a runner; the KFileMetaData
  CMake build and Recoll 1.36's behaviour (the local run used 1.43.0) are unverified on amd64.
- One run, arm64, a Debian kernel, TCG; ext4 lower only; no KASAN.
- Baloo's indexer and Dolphin were not run (no desktop session in the guest); only the library.

## Next step

After merge, dispatch `interop.yml` on `main` and record the run here with a dated note.
