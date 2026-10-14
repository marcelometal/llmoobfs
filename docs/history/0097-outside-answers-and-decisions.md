# 0097 — First answers from outside, and the decisions that followed

Date: 2026-10-13. Status: decisions and plans recorded; no code in this journal. The work on
ADR-0012 that was in progress on the same day is journal 0098.

## What came back from outside

- **linux-btrfs, the question about nested subvolumes** (journal 0083 refuses a nested
  subvolume by testing for inode 256 on a btrfs superblock). A btrfs developer answered on the
  list, thread at
  https://lore.kernel.org/linux-btrfs/a29f5513-1483-4064-bd8a-aa2399076bb7@suse.com/ :
  a subvolume is to be treated as a different filesystem; the inode-256 test is specific to
  btrfs and should not be taken for a general solution; comparing `st_dev` is the common
  practice. On a follow-up he confirmed that every inode inside one subvolume reports the same
  `st_dev`, so checking directories is enough, and added that each subvolume's `st_dev` comes
  from a limited pool of anonymous devices: with very many subvolumes a new one cannot be
  read, which he calls rare in normal use.
- Courtesy replies to three of the thank-you notes of 2026-10-12 (two authors of cited papers
  and the maintainer of virtme-ng). Three other notes did not reach anybody (an automatic
  reply, a list that refuses HTML, an address that bounces).
- A third-party public index of code asked to include one commit of this repository as a
  reference for coding agents. The licence already allows it. The answer given: no objection,
  with the request that the entry be marked as experimental and not as a known-good solution,
  since the commit fixes defects of this project's own.
- A question posted to the LangChain forum with the results of the LangGraph adapter (journal
  0089) is waiting for the forum's moderation. A request for review posted to one Brazilian
  free-software group on 2026-10-13 has no answer; a second group could not be reached.
- No outside person has read the code yet. linux-unionfs and the two direct requests have no
  answer after one day.

## Decisions

1. **Nested subvolumes are to be detected by device, not by inode number.** On a directory
   lookup the layer will call `vfs_getattr()` on the lower child and on its lower parent and
   return `EXDEV` when `stat.dev` differs. It replaces the test of journal 0083, costs a
   `getattr` on directory lookups only, and is not specific to btrfs. Stated on the list as
   what will be done. Not implemented; it comes after ADR-0012. The limit on anonymous devices
   is btrfs's own and does not change what the layer does.
2. **Security roadmap, two items accepted by the maintainer** (`docs/security-roadmap.md`):
   S12, one run of the suite under SELinux enforcing with a confined process, as a bounded
   investigation whose result is either fixes or a documented "not supported"; S13, tracepoints
   for the index (build, release, incomplete), readable by root only, to arrive with the
   second half of ADR-0012. Reason for S12: the layer acts on the lower filesystem with the
   mounter's credentials and has never been loaded where access is decided by label. Reason
   for S13: ADR-0012 adds behaviour that cannot be seen from outside.
3. **ADR-0012, release under memory pressure:** a domain used in the last 60 seconds is not
   released by the shrinker; the time is a mount option. Decided before the code exists.
4. **Returning attribute values with query results** (journal 0072) waits for a third use
   case: the second client reads every result's file anyway (journal 0089).
5. **Not done, on purpose:** the undelivered thank-you notes are not sent again; a paper draft
   written outside the repository is put aside until the project is more mature, to be looked
   at again at the end of November 2026.
6. **Every work used is cited and thanked** (`docs/references.md`, section "Thanks"), as a
   standing rule for new journals and decisions.

## Changes in this journal

`docs/security-roadmap.md` (S12, S13), `docs/references.md` and `README.md` (thanks),
`CHANGELOG.md`, the history index.

## Limitations

- The claims in S12 about how overlayfs, the SELinux test suite and fstests treat labels on
  stacked filesystems were written from memory and are marked there as not verified.
- Nothing here was tested: it is a record of answers and decisions.

## Next step

Finish and measure the first half of ADR-0012 (journal 0098), then the subvolume test by
device.
