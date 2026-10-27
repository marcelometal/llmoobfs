# 0211 — Harness hygiene: workflow steps input, uninstall paths, syzkaller image writes

Date: 2026-10-27. Status: done on `marcelometal/ci-harness-hygiene`. Work started: 2026-10-27
about 15:31 America/Sao_Paulo; ended about 15:35. Calendar time, not an effort measurement.
Estimated effort: S.

## Goal and decision

A source review of the scripts and workflows by a contributor, made with an AI assistant, found
three places where a value reached a shell or a path in a way its author did not intend. None is
reachable by an unprivileged user of the module; each is fixed where it is.

- `kcsan.yml` and `kasan-arm64.yml` pass the `steps` input of a manual run through `env:`, as
  they should, but then put it inside single quotes of the guest's command line
  (`--exec "MOOB_ONLY='$MOOB_ONLY' ..."`), so a quote in the input ran in the guest. Only users
  with write access can start these runs, and the guest is disposable. The step now refuses a
  value with anything but lower-case letters, digits and spaces, which is every step name of
  `vm/suite.sh`.
- `scripts/install.sh uninstall` built its list of files with a command substitution, so a
  `PREFIX` or `DESTDIR` with a space split into words and each word was removed if it was a
  regular file. Each path is now removed on its own.
- `vm/syzkaller/image-setup.sh` runs as root on the host and writes into the syzkaller image
  mounted by loop; an absolute symbolic link inside a tampered image sent the write to the
  host's own `/etc`. The image is now mounted with `nosymfollow`. A tampered cache already means
  the user's account is compromised, so this is defence in depth.

Two further items of the same review were not taken: Dependabot version updates for pip and
Docker (journal 0205 records the choice of security-update pull requests for pip, which already
arrive), and a digest pin for the dev container's docker-in-docker feature, which only
Codespaces use.

## Changes

`.github/workflows/kcsan.yml`, `.github/workflows/kasan-arm64.yml`, `scripts/install.sh`,
`scripts/test-install.sh` (a staging root with a space and a decoy file at its first word),
`vm/syzkaller/image-setup.sh`, the changelog and the history index.

## What ran

- The input filter in bash: `ext4` and `buildrace release` accepted; `ext4' ; id #`,
  `ext4$(id)` and `ext4;id` refused.
- `sh scripts/test-install.sh`: `INSTALL_UNIT_PASS`. Negative round with `scripts/install.sh` of
  `origin/main`: `INSTALL_UNIT_FAIL: uninstall with a space in DESTDIR`.
- `image-setup.sh` in a privileged container of the pinned `debian:trixie-slim` image of
  `vm/Dockerfile`, on two 16 MiB ext4 images made with `mke2fs -d`: on the clean one it
  completed; on one whose `etc/systemd/system/moobfs.service` is a link to `/host-marker` it
  stopped with `Too many levels of symbolic links` and `/host-marker` was not created. With the
  script of `origin/main` the tampered image was accepted and `/host-marker` was written. The
  first container of the corrected run could not set up a loop device; the next two runs did.
- `shellcheck --shell=sh` 0.9.0 (Ubuntu 24.04, as in the `Lint` job) over the 30 files
  `make lint` lists, and `scripts/check-spdx.sh`.
- `actionlint` 1.7.7 (`rhysd/actionlint@sha256:887a259a5a534f3c4f36cb02dca341673c6089431057242cdc931e9f133147e9`) over every workflow: no finding.

## Limitations

The workflows were not run: the change only adds a check before the existing commands. The
syzkaller script was tested on synthetic images, not on the image syzkaller's
`create-image.sh` builds.

## Next step

Review and merge.
