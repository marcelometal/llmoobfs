# 0189 — G1 adapter package: host checks passed, mounted gate not run

Date: 2026-10-24. Status: adapter package written; G1 not complete.

Status correction, 2026-10-24 evening: the mounted gate passed on the
dedicated Debian host. The original "what did not run" list is superseded by
the follow-up below. G3 was not part of that pass.
Work started: 2026-10-24 18:24 America/Sao_Paulo, when the maintainer said to
continue. Wall-clock time, not an engineering-effort measurement. This unit is
part of the G1 estimate L. It does not close that estimate.

## Goal

Implement the G1 package from
[the protocol](../g6-evaluation-protocol.md) in the existing LangGraph adapter,
without changing `layer/` or the UAPI. Run the checks this host can run. Do not
treat a missing mount as a pass of the mounted gate.

## What the adapter does now

`bench/client/langgraph_store.py`:

- A file takes read/write for every class that can enter its domain directory,
  set with `fchmod` on the descriptor. When the group can enter, the file's
  group is the domain's group. Private `0700` stays `0600`. Shared `2770`
  becomes `0660`.
- Publication is still temporary file, content, typed attributes, `fsync`,
  `rename`, directory `fsync`. A test can stop after each step. A stop before
  `rename` leaves the previous final name intact. The temporary name is unique
  per attempt, so a leftover temporary does not block the next publication.
- `ENOMEM`, `E2BIG`, `ETIMEDOUT` and `EDQUOT` from `QUERY2` raise
  `MoobQueryError`. Pages already copied are not returned.
- When `statfs` is not the layer, search opens the files the caller can open
  and does not issue `QUERY2`. A mounted domain is not also scanned. Text
  search still raises `NotImplementedError`.

## What ran

Host: macOS, Python 3.15, langgraph 1.2.15 and langgraph-checkpoint 4.3.0 in a
venv under `build/` (gitignored, not a project dependency). Command:

```text
build/g1-venv/bin/python bench/client/g1_protocol.py
```

Result: `G1_HOST_PASS`. Also printed `G1_CONCURRENT last-rename-wins note=left`.
The checks covered the kind and flag quotas for 1 000 and 5 000 slots, source
counts for `os.access`, sqlite and a side database, file modes under umask
`077`, publication prefixes, the twelve filter shapes against `InMemoryStore`
on a small corpus (8, 16 and 16 files), client pages of limit 2, omission of a
mode `000` file the process could not open, a fresh put and ten deletes, a
mounted search that does not scan, a fake `QUERY2` that continues a cursor and
one that raises `ENOMEM` on the second page, and one complete file after two
concurrent publishers.

The same run printed `G1_MODULE_NOT_RUN`, `G1_BUDGET_NOT_RUN`,
`G1_CONFINE_NOT_RUN` and `G1_GET_INFO_NOT_RUN`. It was repeated at 18:34 with
the same lines. `git diff --check` passed on the tracked edits. The new files
had no trailing whitespace or tabs. `sh scripts/check-retired-claims.sh`
returned `RETIRED_CLAIMS_OK`. `python3 scripts/check-site.py .` passed for 12
HTML pages. `python3 scripts/site-faq.py --check` matched the FAQ. A local
link check resolved 289 relative Markdown destinations in the changed
documents.

## What did not run

No moobfs mount. No uid 1001 and uid 1002. No 5 000-file `QUERY2`. No
`salary.json` under the homogeneity rule. No `index_budget=1M` drill. No mount
namespace. No `GET_INFO` record. No VM, KASAN or benchmark. The fake ioctl does
not establish that the kernel cursor or the budget refusal behaves that way.
G3 was not started. G1's pass conditions that need the module are unmet.

## Changes

The adapter, `bench/client/g1_protocol.py`, this journal, the history index,
the changelog, the roadmap status, the protocol's implementation note, the
client-contract pointer, the references row and a follow-up on journal 0188.

Both READMEs, the site, the FAQ, `SECURITY.md` and `docs/help-wanted.md` stay
as they were. No tested module behavior and no support commitment changed.
The help page still points at the journal 0089 comparison, which this run does
not replace.

## Next step

Run the mounted gate on the dedicated Mac mini (Debian), which has the module,
uids 1001 and 1002, and a separate `index_budget=1M` mount. Until that gate
passes, G1 stays open. G3's drill remains the following package.

## Follow-up, 2026-10-24

The maintainer required that nothing be installed on the machine where this
journal was written. The gitignored venv `build/g1-venv` was removed the same
evening. The system Python on that machine does not import `langgraph`. The
host-check result above stays a record of the run; the packages are not kept
there. Further execution is on the Mac mini.

## Follow-up, 2026-10-24 evening — mounted gate passed

The first mounted run, on the dedicated Debian host (kernel
`6.12.111+deb13-amd64`, Python 3.13.5, the same LangGraph pins in a venv on
that host), planted the corpus and then failed:

```text
G1_MOUNTED_FAIL shape {'kind': 'decision'} team 0 vs 625 calls=1
```

`alice` and `bob` had matched. `team` is owned by root, mode `2770`. The
files were `0660`, owned by uid 1001, group `moobteam`. `reach_ok` indexes a
file that shares the domain directory's owner and group, or a file readable
by owner, group and other. Uid 1001 cannot chown a file to root, so those
`0660` files stayed out of the index. The adapter now adds other-read when
the publisher is not the domain owner. On this corpus the `team` files are
`0664`, uid 1001, group `moobteam`. A publisher who already owns the domain
still gets `0600` or `0660`. The host check was re-run on the same venv after
that change and printed `G1_HOST_PASS` and `G1_CONCURRENT last-rename-wins
note=left`.

The second mounted run printed `G1_MOUNTED_PASS` and exited 0. Wall clock of
that process was 130 seconds, including publication of the 7 000-file corpus
and the separate 5 000-file budget corpus. It is not a latency result.

Recorded on ext4 (the host's test volume, non-rotational):

- `GET_INFO` at mount: `index_budget=134217728`, `index_used=0`.
- After planting, before any query: `index_used=0`, `index_inodes=0`,
  `domains_built=0`. The index is still built on the first query.
- After the twelve shapes, the full `team` scan and the client pages:
  `index_budget=134217728`, `index_used=2883508`, `index_inodes=7000`,
  `domains_built=3`. `VmHWM` of that same root process, which had also
  planted the files, was 59 804 kB. There is no separate sample of a process
  that only searched, and no cold-build sample between the first query and
  the later ones. No ceiling was applied.
- `team/m00000.json` was mode `0664`, uid 1001, gid of `moobteam`.
- The full `team` scan used at least two `QUERY2` calls and returned 5 000
  items. Client pages of the `decision` filter matched the unpaged answer,
  and that answer had 625 items.
- Confined uid 1001 searched `alice` without the mount root.
- Thirty concurrent publications stayed complete files. The reader counted
  86 in-flight misses across 30 searches of 30 keys. That count is a record,
  not a failure.
- On plain ext4, uid 1001 did not see `salary.json` and uid 1002 did.
- On the module, `salary.json` was absent for uid 1001, uid 1002 and root.
  Uid 1001's `open` failed. Uid 1002's `open` succeeded.
- Separate mount, `index_budget=1M`, 5 000 files, eight typed attributes:
  the search raised `MoobQueryError` with `ENOMEM` and no list. The file
  count was not changed.

No VM, KASAN, lockdep or benchmark comparison was run. btrfs and XFS were
not part of this gate. The host users `alice` (uid 1001), `bob` (uid 1002)
and the group `moobteam` were created for the run and left in place. The
mounts were still up at the end of the run. The same evening the roadmap,
the changelog, the protocol note, the client contract, the references row
and the documentation index were brought in line with this pass. The root
README, the site, the FAQ, `SECURITY.md` and `docs/help-wanted.md` do not
state this gate, so they were left as they were.

## Follow-up, 2026-10-24 evening — G3

Journal [0190](0190-g3-restore-drill.md) ran the identity-map restore drill on
the lower tree this gate left behind. The mounts from this run were gone
after that drill, and the module was unloaded. The users and the lower tree
remain on the host.
