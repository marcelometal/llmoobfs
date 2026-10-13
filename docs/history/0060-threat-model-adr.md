# 0060 — S1: threat and credential model proposed (ADR-0011)

Date: 2026-10-12. Status: ADR proposed, awaiting the maintainer; local series step 2.

## Goal

Write the threat and credential model (security roadmap S1, community action 16) so that reviews
and the remaining S-items test against a stated contract, and settle the two questions journals
0028 and 0030 left open.

## How it was written

From the code on `main`, not from earlier plans: the filesystem type flags, every
`override_creds`/`backing_file_open` call in `layer/main.c`, the ioctl dispatch and permission
checks in `layer/index.c`, the mount scan, and `docs/spec-query-v2.md` for the domain rules.

## Content

- Adversaries: unprivileged local user (main), a process writing to the lower directly, the lower's
  content at mount. Trusted: root and `CAP_SYS_ADMIN`/`CAP_DAC_OVERRIDE`, the mounter, the kernel.
- Credentials as implemented: mount needs `CAP_SYS_ADMIN` (no `FS_USERNS_MOUNT`, no
  `FS_ALLOW_IDMAP`); name operations use the caller's credentials and the lower's own permission
  check; data operations on an open file use the mounter's credentials after the caller's open was
  authorized (the overlayfs model).
- Five promises, five non-promises, the list of unprivileged surfaces.
- Journal 0028 (path-based LSMs): not part of the contract; deny the domain directory instead.
- Journal 0030 (timing channel): closed for `QUERY2`; kept for `domain_depth=0` as a documented
  administrator's choice.

## Findings

- **F1:** `GET_INFO`, open to anyone who can open the mount root, returns index totals that sum
  every user's memories; polling them shows another user's write rate. It contradicts
  `SECURITY.md`. Proposed fix (next code step): counters only for `CAP_SYS_ADMIN`.
- **F2:** one index budget per mount; an unprivileged writer can fill it and leave the index
  incomplete for everyone until remount. Tracked under S9.
- A case made explicit: a file made private directly on the lower stays visible by name and
  attributes in its domain's `QUERY2` results until remount (not openable).

## Changes

ADR-0011; `SECURITY.md` "Current boundaries" (the `QUERY` description predated domains) links it;
`docs/README.md` lists it.

## Limitations

Proposed, not accepted. F1 and the `moobfs-check` line that names the model are not implemented.
No outside review yet (community action 15 comes after acceptance and F1).

## Next step

Maintainer review of ADR-0011; then fix F1 with a test, still in the local series.
