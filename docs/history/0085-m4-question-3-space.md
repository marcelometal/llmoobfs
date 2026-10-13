# 0085 — M4 question 3: space saved by reflink and dedupe through the layer

Date: 2026-10-13. Status: measured once per file system on the synthetic data set; not measured
on a real client's store. Local series (after pull request 60).

## Goal

Question 3 of the benchmark plan had no number at all: reflink and dedupe were only shown to
work (journals 0012 and 0077). This journal measures what they save on the data set the other
M4 campaigns use.

## Method

`bench/q3-space.sh`, described in `bench/results/2026-10-13-macmini-q3-space/README.md`. Dedupe
is whole-file and goes through the layer; so do the reflink and plain copies.

Tool note: `duperemove` 0.11.2 was tried first and found no candidate, through the layer and on
the bare lower alike: it works on blocks larger than these files. It says nothing about the
layer and is not used.

## Data set

60 000 files, 182 657 997 bytes of content (mean 3 044 bytes): 30 229 files of at most 2 048
bytes, 16 670 between 2 049 and 4 096, 13 101 larger. Rounded up to 4 KiB blocks the content is
325 300 224 bytes. 50 898 distinct contents; 9 102 files are a byte-for-byte copy of another one
(27 310 643 bytes).

## Results

| | btrfs | XFS |
| --- | --- | --- |
| a plain copy of the tree uses | 432 463 872 | 359 321 600 |
| dedupe calls through the layer, accepted / refused | 9 102 / 0 | 9 102 / 0 |
| bytes used after dedupe | 403 587 072 | 310 296 576 |
| saved by dedupe | 28 876 800 (6.7 %) | 49 025 024 (13.6 %) |
| a second copy with reflink, through the layer | 204 242 944 (47 % of a plain copy) | 34 054 144 (9.5 %) |
| a second copy without reflink, through the layer | 432 889 856 | 359 202 816 |

## Reading

- **Dedupe through the layer works and frees what the lower can free.** Every call was
  accepted. With 15 % of the files being copies, the saving is 7 % on btrfs and 14 % on XFS.
- **Reflink copies are cheap on XFS and half-price on btrfs.** A cloned tree costs a tenth of a
  plain copy on XFS. On btrfs it costs almost half, because half of these files are at most
  2 KiB and btrfs keeps such files inside its metadata, where nothing is shared; the inode and
  directory entries are new in both cases.
- **Where the layer gains nothing:** the saving is the lower filesystem's, with or without the
  layer. The layer adds no sharing of its own, and on ext4 there is none to delegate.
- **For small memories the gain is small.** Agent memories in this profile average 3 KiB. A
  filesystem cannot share less than a block, and btrfs cannot share an inline file at all. The
  larger the files, the more of the content these numbers would apply to.

## Limitations

- One run per file system; one data set; its duplication is the generator's parameter (15 %),
  not a property of real agent memory. Whether real stores contain duplicate files at all was
  not measured.
- The first real client (ai-memory) was not measured on btrfs or XFS.
- The module was the one of journal 0069; the dedupe and clone paths changed in journal 0074
  (privileges dropped first) in ways that do not affect byte counts.
- Latency of clone and dedupe through the layer against the bare lower was not measured.
- btrfs counts depend on its inline-file threshold (the default was used).

## Next step

Question 2: latency per POSIX operation through the layer against the bare lower, on the current
module, when the host is idle. Then the report.
