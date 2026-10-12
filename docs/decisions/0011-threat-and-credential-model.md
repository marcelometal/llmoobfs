# ADR-0011 — Threat and credential model of the stacked layer

Date: 2026-10-12. Status: **accepted by the maintainer on 2026-10-12**. Item S1 of
`docs/security-roadmap.md`; community action 16. Describes the module as implemented on `main`
(ADR-0007, ADR-0009, phases Q2a to Q2e); where the code and this text disagree, the findings
below say so and a later step fixes the code, not the text.

## Context

Reviewers cannot judge a stacked filesystem's credential scheme without a written model; the
overlayfs `override_creds=off` discussion is the usual example. `SECURITY.md` already says what
counts as a vulnerability, and journals 0028 and 0030 left two questions for this ADR (path-based
LSMs and the `QUERY` timing channel). This ADR fixes the adversary, the promises, the non-promises
and the unprivileged surfaces, so that S2, S4, S9 and outside reviews test against a stated contract.

## Assets

- The contents, names and `user.moob.*` attributes of each user's memories.
- The integrity and availability of the running kernel.
- The data on the lower filesystem.

## Adversaries

- **A1 — Unprivileged local user** with ordinary access to the mount point, including an agent
  running under a user's own uid without `CAP_SYS_ADMIN`, `CAP_DAC_OVERRIDE` or
  `CAP_DAC_READ_SEARCH`. This is the main adversary.
- **A2 — A process writing to the lower tree directly** while the layer is mounted, bypassing it.
- **A3 — The lower tree's content on disk** at mount time, prepared by whoever could write to it
  (A2 earlier, another system, a restored backup).

Trusted: root and holders of `CAP_SYS_ADMIN` or `CAP_DAC_OVERRIDE` in the initial user namespace,
the mounter, the kernel and the lower filesystem's code, the hardware and the block device.

## Credentials (as implemented)

- **Mounting** needs `CAP_SYS_ADMIN` in the initial user namespace: the filesystem type sets no
  `FS_USERNS_MOUNT` and no `FS_ALLOW_IDMAP`, so user-namespace mounts fail with `EPERM` and
  idmapped mounts with `EINVAL` (journal 0020). This removes the class of overlayfs CVEs that
  needed unprivileged mounts (CVE-2015-1328, CVE-2016-1576, CVE-2020-16120). It is a decision, not
  an accident: enabling either flag needs a new ADR.
- **Name operations** (lookup, create, mkdir, unlink, rmdir, rename, link, symlink, setattr,
  `setxattr`/`removexattr`, ACLs) run with the **caller's** credentials, and `permission()` is the
  lower inode's own check with the caller's credentials. New files belong to the caller.
- **Data operations on an open file** (read, write, mmap, fsync, fallocate, FIEMAP, reflink,
  dedupe, `copy_file_range`, directory reads) run with the **mounter's** credentials captured at
  mount time, through `backing_file` or `override_creds`, after the VFS checked the caller's
  permission on open. This is the overlayfs model; it is safe only because the open was authorized
  for the caller first.
- `user.moob.format` on the lower root is reserved to the tools (`EPERM` through the layer).

## Promises

Against A1:

1. **No crash, hang, memory corruption or kernel warning**, denial of service included.
2. **No privilege escalation**: the layer never lets the caller do on the lower what the lower
   would refuse to the same caller directly.
3. **No exposure beyond the lower through the query interface.** `QUERY2` is issued on a domain
   directory, checks `MAY_EXEC | MAY_READ` on it with the caller's credentials, and answers only
   from that domain's partition, which holds only files every traverser can read (homogeneity
   rule, `docs/spec-query-v2.md` §3; fallback to per-result checks after a permission change of a
   directory). It does not walk other domains, so its time does not depend on them (journal 0043).
   With domains on, v1 `QUERY` is limited to `CAP_SYS_ADMIN`.
4. **Typed attributes are validated** in the kernel on every write through the layer, with the
   same code as the tools (`common/attr.c`).

Against A2 and A3:

5. **No crash, hang or memory corruption**, whatever the lower contains or however it changes.
   Attributes read from the lower are untrusted: the mount scan skips invalid names and encodings
   (they are not indexed; `moobfs-check` reports them) and never fails the mount for them.

## Non-promises

1. **Confidentiality against trusted parties** (root, `CAP_DAC_OVERRIDE`, access to the device) and
   at rest. For that: dm-crypt or fscrypt on the lower.
2. **Integrity against A2.** Changes made behind the layer reach the index only at the next mount;
   until then `QUERY`/`QUERY2` may return stale or missing results (never results from a domain the
   caller cannot traverse). This includes a file made private directly on the lower (`chmod 600`
   behind the layer): until remount, other traversers of its domain can still see its name and
   attributes in `QUERY2` results, though not open it. Changing permissions through the layer
   takes effect on the next call. `moobfs-check` detects the divergence; nothing does in real time.
3. **Path-based LSMs** (AppArmor, Landlock) do not filter query results. A confined process can see
   in the results a name it could not open; it already sees it by listing the directory and reading
   its attributes, which Landlock does not control (journal 0030). To hide a domain from a
   confined process, deny it the domain directory itself.
4. **`domain_depth=0`** keeps v1 open to everyone with per-result checks and its timing channel
   (about 1 µs per hidden match, journal 0030). It is an administrator's choice for single-user
   mounts and is documented as such.
5. **Fair sharing of the index budget** (finding F2 below) and other resource limits (S9).

## Unprivileged surfaces

`getxattr`, `setxattr`, `removexattr` and `listxattr` on `user.moob.*`; `MOOB_IOC_QUERY2` and
`MOOB_IOC_QUERY2_BATCH` on a domain directory; `MOOB_IOC_GET_INFO` on the mount root;
`FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE`, `FIEMAP` and `copy_file_range` (delegated, mounter's
credentials after an authorized open); every ordinary file and directory operation. There are no
export operations, so `open_by_handle_at` does not apply. These are the targets for S3 (syzkaller)
and S7 (fuzzing).

## Findings recorded with this ADR

- **F1 — `GET_INFO` counters cross users.** Any process that can open the mount root gets the
  index totals (`index_entries`, `index_inodes`, `index_used`, `heterogeneous`,
  `fallback_domains`), which sum every user's memories. Polling them shows another user's write
  rate. This contradicts promise 3 and `SECURITY.md` ("including through counts"). Proposed fix:
  the counters only for `CAP_SYS_ADMIN`, zero for others, with the static fields (limits, lower
  type, capabilities, flags) unchanged; a UAPI note and a test. **Fixed on 2026-10-12** (journal
  0061): `MOOB_FLAG_COUNTERS` marks a reply that carries them.
- **F2 — One budget for all users.** `index_budget` (128 MiB by default) is shared by the mount. A1
  with write access can fill it with attributes; the index is then marked incomplete for everyone
  until remount (`MOOB_FLAG_INDEX_INCOMPLETE`, one `pr_warn`). Not a crash, but a cross-user
  degradation of the query service. Kept as a known limitation under S9 (per-uid or per-domain
  accounting is the candidate); `GET_INFO` and `moobfs-check` make it visible.

## Decision

Adopt the adversaries, credentials, promises and non-promises above as the contract that
`SECURITY.md`, reviews and the S-items test against. Fix F1 before asking for independent review
(community action 15); track F2 in S9. `moobfs-check` prints the ADR number of the model it was
built against, so a report can say which contract it was judged by.

## Alternatives considered

- **Overlayfs-style `override_creds` for name operations too** (mounter's credentials everywhere):
  rejected; it would make every name operation depend on a correct emulation of the lower's
  permission checks, the source of most overlayfs credential bugs.
- **Enable `FS_USERNS_MOUNT`** for rootless agents: rejected for now; it reopens the CVE class
  above. Rootless use goes through an administrator-mounted layer.
- **Filter query results by path-based LSMs**: rejected; the kernel has no general hook to ask
  "could this process open this path" without opening it, and the gain over listing is small
  (journal 0030).

## Consequences

S4 tests promise 5 with adversarial lower trees; S2 (fstests) and S3 exercise promises 1 and 2; F1
is the next code step of this series. `SECURITY.md` links this ADR as the model reports are judged
against.
