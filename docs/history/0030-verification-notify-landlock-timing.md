# 0030 — Verifications: change stream, Landlock and `QUERY` timing channel

Date: 2026-10-11. Status: completed (experiments on the Azure host).

## Goal

Verify in practice three points that the research (`docs/research-metadata-search.md`) left as
not verified or inferred, all relevant to ADR-0009: whether an external index can be fed by
kernel notifications for changes made through the layer; whether processes confined by Landlock
see results they cannot open; and whether the response time of `QUERY` reveals entries the
caller cannot see.

## Tools

`tests/notify.c` (`fanotify` watcher with a filesystem mark and `FAN_REPORT_DFID_NAME`, and
per-directory `inotify`) and `tests/landlock_run.c` (runs a command under Landlock: listing
allowed under a root, reading files only under one folder and the executable's folder). Azure
host, kernel `6.12.112-moobfs-bench`, active LSMs
`lockdown,capability,landlock,yama,apparmor,ima,evm`, layer over ext4. Static binaries in
`/opt/moob-test` so they can run as uid 1001.

## 1. Change stream

Operations through the layer: create `f1`, write two attributes, rename to `f2`, create folder
`d1`, remove `f2`.

| Watcher | Result |
| --- | --- |
| `fanotify`, filesystem mark on the layer | refused: `Operation not supported` |
| `fanotify`, filesystem mark on the lower ext4 | 7 events: CREATE f1, ATTRIB f1 (2), MOVED_FROM f1, MOVED_TO f2, CREATE d1, DELETE f2 |
| `inotify` on the layer folder | the same 7 |
| `inotify` on the corresponding folder of the lower | the same 7 |

Conclusion: the change stream ADR-0009 needs already exists without new kernel code, by watching
the lower filesystem with `fanotify` (one mark, requires `CAP_SYS_ADMIN`) or each layer folder
with `inotify`. The layer refuses `fanotify` with file identifiers because it does not implement
the export operations (`encode_fh`); overlayfs implemented those operations to allow this, and it
would be a contained change if needed.

## 2. Landlock

Six memories with `kind = lt`, three in `lt/allowed` and three in `lt/denied`, readable by
everyone. As uid 1001:

- without Landlock: `QUERY` returns the 6, and all of them open;
- with Landlock (listing under the mount root, reading files only in `lt/allowed`): `QUERY`
  returns the same 6; `open` of `lt/allowed/m1.md` works and `open` of `lt/denied/m1.md` fails
  with `EACCES` (client code 3);
- with the same Landlock, `moobfs-attr get` (`getxattr`) of the `kind` attribute of a denied file
  works.

Conclusion: the gap exists (results the process cannot open), but it is narrow. Landlock
(ABI 6) does not control reading of extended attributes, and using `QUERY` requires being able to
list the mount root, which in Landlock applies to the whole tree below it. `QUERY` therefore
reveals names and attribute values that this process would already obtain by listing folders and
reading attributes; the content stays protected. AppArmor, which can deny subfolders by path, was
not tested.

## 3. Timing channel

Uid 1001 has 10 memories `kind = tc` in a folder of its own; uid 1002 has N memories `kind = tc`
in a 0700 folder. Uid 1001 runs `kind eq string tc` 400 times (`bench-load --uid 1001`).

| N (private to uid 1002) | Results seen by uid 1001 | p50 | p10 | p90 |
| --- | --- | --- | --- | --- |
| 0 | 10 | 18.2 µs | 18.1 | 18.3 |
| 1 000 | 10 | 1.15 ms | 1.14 | 1.17 |
| 5 000 | 10 | 5.88 ms | 5.75 | 6.13 |
| 20 000 | 10 | 28.4 ms | 28.2 | 28.6 |

Conclusion: the time grows about 1.15 µs per invisible candidate, with dispersion below 3%.
An agent can estimate how many memories of other owners match a predicate and test the existence
of a value anywhere in the mount. The phase 3b promise of revealing nothing "nem em contagens"
("not even in counts") holds for the returned results and counts, not for the time. Documents
corrected: `docs/spec-m3.md` §3.1, `docs/faq.md`, `docs/architecture-overview.md`,
`docs/testing.md`; note in S1 of `docs/security-roadmap.md`.

## Implications

- Change stream for an external index: available today (`fanotify` on the lower or `inotify` on
  the layer); it does not justify a cursor of our own now.
- The timing channel only closes structurally when the query does not walk entries of other
  permission domains: an index partitioned by domain, in the kernel or outside it (the GUFI model
  of ADR-0009). Mitigations (reusing per-directory authorization, ADR-0008) reduce the
  per-candidate cost, but do not eliminate it.
- Constant-time padding was discarded as a mitigation: it would cost the worst case on every
  query.

## Limitations

One host, ext4, one run of each experiment; AppArmor not tested; Landlock tested at ABI 6 of
kernel 6.12.
