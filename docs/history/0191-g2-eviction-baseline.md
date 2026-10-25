# 0191 — G2 eviction baseline recorded, fair share not implemented

Date: 2026-10-24. Status: baseline recorded. G2 is not implemented and did
not pass.
Work started and ended on 2026-10-24, 19:07 to 19:08 America/Sao_Paulo, for
the mounted process. Wall-clock time, not an engineering-effort measurement.
The G2 estimate remains L.

## Goal

See whether the current module, which already has the ADR-0019 build guard,
still releases one user's domain to make room for another. Register the
fairness property before choosing a share algorithm. Do not treat the
recording as a pass.

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, the module built
earlier the same evening, ext4, `index_budget=1M`, `domain_depth=1`.
`bench/client/g2_baseline.py` planted 2 000 files in a domain, searched it,
then did the same for a second domain and searched the first again. Each
file has four scalars. The process printed `G2_BASELINE_RECORDED` and
exited 0. It took 78 seconds, including the publications. That duration is
not a latency result.

Cross-user, uid 1001 then uid 1002. `GET_INFO` as root:

- after uid 1001: count 2000, `index_used=840600`, `index_inodes=2000`,
  `domains_built=1`, `domain_builds=1`, `domain_releases=0`;
- after uid 1002: count 2000, `index_used=836600`, `index_inodes=2000`,
  `domains_built=1`, `domain_builds=2`, `domain_releases=1`;
- uid 1001 again: count 2000, `index_used=840600`, `index_inodes=2000`,
  `domains_built=1`, `domain_builds=3`, `domain_releases=2`.

Same uid, two domains, the same shape: `domain_releases` went 0, 1, 2 and
`domain_builds` went 1, 2, 3. `index_used` stayed at 838600 and one domain
stayed built. Every search returned 2000. No resource errno.

The two cases are the same ping-pong. A build that does not fit releases
the other built domain, whoever owns it, and the released domain is rebuilt
from the lower tree on its next search. The answers stay complete. The cost
is the release and the extra build. Both domains are never resident together
at this size: 840600 bytes for 2000 inodes does not leave room for a second
copy inside 1048576 bytes.

The 4000-file retry did not run. The 2000-file size already released.

## What was not decided

No share algorithm. No mount option. No change to `layer/` or the UAPI.
Section 9 of the protocol states the property a later implementation must
meet: uid 1002's build must not release uid 1001's domain, while one uid
may still release that uid's own other domains. A latency ceiling is not
set, so this property does not by itself complete G2.

No VM, KASAN or benchmark. btrfs and XFS were not used. After the record
the drill's mounts were gone. The lower trees `g2-0187-cross` and
`g2-0187-same` remain on the host's ext4 test volume.

## Next step

An ADR for how a second uid obtains room without taking the first uid's
domain, then a negative run of this baseline and the kernel change. Until
that exists, G2 stays open.

Follow-up, 2026-10-25: journal 0192 and ADR-0021 did that. The host
regression failed on this module and passed after the filter. The numbers
above remain the record of the previous module. G2 stays open because the
latency ceiling is unset.
