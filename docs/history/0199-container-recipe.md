# 0199 — Container recipe, validated (roadmap C3)

Date: 2026-10-25. Status: done; both runs passed. Branch `claudiofp/CONSUMABLE-C3`. Work started
2026-10-25, after PR 144 merged, and ended the same evening. Labor time was not measured; calendar
time is recorded per run (run 1 several hours under TCG, run 2 11 minutes under KVM).
Estimated effort in the roadmap: M.

## Goal

Item C3 of the consumable path (ADR-0022, tracking issue 145): turn §2.5 of
`docs/distribution-strategy.md` into a tested recipe for Docker and Podman. The host mounts, and
each container receives a bind mount with aligned uids. The item also tests the alternative §2.5
left open (a read-only bind of the whole root) and states which runtimes are refused or untested.

## Decisions

- **The container test reuses the install-test VM.** `vm/install-test/in-container.sh` now takes
  `MOOB_TEST=container`, which runs `vm/container-test/guest.sh` in two phases: packages, Docker
  (Debian's `docker.io`), Podman and the listed kernel, then a reboot, `make install`, the mount
  of `docs/operations.md` and every container case. The cloud image is cached once for both tests.
  Alternative not taken: a separate driver. It would have duplicated the boot, cloud-init and SSH
  code.
- **The container image is the one already pinned** in `vm/Dockerfile`
  (`debian:trixie-slim@sha256:a29215f6…`, a multi-architecture index digest). The MoobFS tools
  are bound read-only into it from the host, so nothing is built in the container.
- **Expectations are written from the code, and observed where the code cannot tell.**
  `query2_target()` in `layer/index.c` computes the domain from the dentry's depth below the
  superblock root, never from the mount, so a bind of a domain directory should answer
  `QUERY2`. `GET_INFO` and `QUERY` compare the dentry with `sb->s_root`, so they should not
  answer through it. Those cases assert. What a runtime does when the kernel refuses an
  idmapped mount (Podman's `:idmap`) is runtime behaviour, so the test prints what it observes
  and fails only on evidence of translated owners.
- **Rootless Podman** (alice's own Podman, `--userns=keep-id`) is reported, not required. The
  item's acceptance allows "stated as refused or untested".
- **A run can use KVM.** `MOOB_HOST_QEMU=1` runs QEMU on a Linux host instead of in Docker,
  under KVM when `/dev/kvm` is writable. Nothing of MoobFS is installed or loaded on that host.

## Where the runs happen

Run 1 used Docker and QEMU TCG on the maintainer's Apple Silicon Mac, as the earlier install
tests did. On 2026-10-25 the maintainer decided that VM runs move to the Mac mini (x86, Debian,
KVM), and that what the tests installed on the Mac is removed afterwards. When run 1 ended, the
Mac mini was running the evolution front's G5 measurements (load about 11 on 8 threads for more
than four hours). Run 2 waits for them to finish, so as not to skew their numbers.

## Evidence

Run 1 (`make container-test`, Apple Silicon Mac, TCG, Docker 26.1.5, Podman 5.4.2):

```text
BIND_RW_docker: read, write and setxattr through the domain bind; invalid value refused by the kernel
QUERY2_DOMAIN_BIND_docker: answered inside the container (2 results)
INFO_DOMAIN_BIND_docker: refused with exit 69, as documented
SECOND_AGENT_docker: uid 1002 sees only its own directory
WRONG_UID_docker: alice's directory under uid 1002 is refused (read and query, exit 77)
RO_ROOT_BIND_docker: QUERY2 and GET_INFO work through it; writes refused; bob's domain refused; top-level names visible
BIND_RW_podman: read, write and setxattr through the domain bind; invalid value refused by the kernel
QUERY2_DOMAIN_BIND_podman: answered inside the container (3 results)
INFO_DOMAIN_BIND_podman: refused with exit 69, as documented
SECOND_AGENT_podman: uid 1002 sees only its own directory
WRONG_UID_podman: alice's directory under uid 1002 is refused (read and query, exit 77)
RO_ROOT_BIND_podman: QUERY2 and GET_INFO work through it; writes refused; bob's domain refused; top-level names visible
MOOBFS_CONTAINER_FAIL: an idmapped bind of the layer was accepted
```

Podman's count is 3 because Docker's container had already written `c-docker.md`. A remapped uid
1001 (`--uidmap 0:200000:65536`) could not read alice's note, as expected.

The run stopped on the case written as "an idmapped bind (`-v dir:/memory:idmap`) is refused":
`podman run … :idmap … true` exited 0. That command reads nothing, so it cannot tell a refused
idmap that Podman silently replaced with a plain bind from an idmapped mount that took effect.
The second would contradict the module, which does not set `FS_ALLOW_IDMAP`. The case now prints
what the container sees (the mount's line in `/proc/self/mountinfo`, the owner of a file, whether
the remapped uid 1001 can read it). It fails only if owners were translated or the mount is
marked `idmapped`, and run 2 decides it. The serial console logs of run 1 contain no `BUG:`,
`Oops` or `WARNING:` line.

Run 2 (`MOOB_HOST_QEMU=1 MOOB_TEST=container sh vm/install-test/run.sh`, commit b8bbded, the
Mac mini under KVM after G5 finished, Docker 26.1.5, Podman 5.4.2), 660 s:

```text
BIND_RW_docker … RO_ROOT_BIND_docker: the six Docker cases as in run 1
BIND_RW_podman … RO_ROOT_BIND_podman: the six Podman cases as in run 1
USERNS_REMAP_REFUSED: a remapped uid 1001 cannot read alice's files
IDMAP_BIND_OBSERVED: exit 0: 345 344 0:53 /alice /memory rw,relatime - moobfs_layer none rw,lower=/srv/moobfs/lower,nolinks stat: cannot statx '/memory/note.md': Permission denied read=no
IDMAP_NO_TRANSLATION: no idmapped mount of the layer took effect (exit 0)
ROOTLESS_KEEP_ID: alice's rootless Podman with --userns=keep-id queried her domain
ELAPSED: 660 s under KVM
MOOBFS_CONTAINER_PASS: docs/container-integration.md followed on Debian 13 with Docker and Podman
```

The `:idmap` question is settled: Podman 5.4.2 starts the container, but the mount is a plain
bind (no `idmapped` in its options) and the remapped uid 1001 cannot even `stat` alice's note, so
no owner was translated. The kernel refused the idmapping, as `can_idmap_mount()` does for a
filesystem without `FS_ALLOW_IDMAP`, and Podman went on without it. The recipe's table said
"refused"; it now describes this. Alice's own rootless Podman with `--userns=keep-id` queried her
domain, because keep-id maps her host uid to the same uid in the container; the recipe gains a
section for it and the README, its Portuguese mirror and `when-not-to-use.md` no longer say "no
rootless containers" (old wordings added to `docs/retired-claims.txt`). The serial console shows
the 6.12.113 and 6.12.111 boots and only the expected unsigned-module taint lines.

While updating the surfaces, `scripts/check-retired-claims.sh` with the new claim "a stable UAPI,
a CLI," found that PR 144 had left "a CLI" in the not-implemented lists of the README, its
Portuguese mirror, `docs/faq.md` and `site/faq.html`. They are corrected here.

The background watcher that was to report the end of G5 never fired: `pgrep -fc` run through
`ssh … 'cmd'` counts the remote shell whose command line contains the pattern, so it never
reached 0. The end of G5 was found by hand (load 0.00).

## Limitations

- One kernel (Debian 6.12.111), one distribution, x86-64, Docker 26.1.5 and Podman 5.4.2 only.
- Not tested: rootless Docker, Docker's `userns-remap` daemon mode, Kubernetes and other
  runtimes, SELinux-labelled binds (`:z`, `:Z`).
- That Podman continues without telling the user that `:idmap` did not apply is the runtime's
  behaviour on this version; the safety comes from the kernel, which refuses the idmapping.
- `GET_INFO` through a domain bind remains refused; the read-only root bind exposes the top-level
  names. Both are stated in the recipe; changing either is a kernel decision for the core front.

## Next step

Publish C3 after the maintainer's approval, then the next consumable item in the agreed order of
`ROADMAP.md` (tracking issue 145). Later VM runs go to the Mac mini under KVM, and what the tests
installed on the Apple Silicon Mac is removed.
