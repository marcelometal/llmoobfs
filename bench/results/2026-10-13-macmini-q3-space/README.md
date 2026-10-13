# Space saved by reflink and dedupe through the layer — Mac mini, 2026-10-13

M4 question 3 (`docs/benchmark-plan.md`). Host and conditions: journal 0042 (Mac mini Server
2011, Debian 13, kernel `6.12.111+deb13-amd64`, one SATA SSD with a btrfs and an XFS partition).
Module loaded: the one of journal 0069. Analysis: journal 0085.

Data set: the 60 000 memories of `../2026-10-12-macmini-q2/` (`bench/gen --seed 1 --inodes 60000
--profile 8`), as already stored on each partition. Its duplicate content is a parameter of the
generator, not a measurement of real agent memory.

Command, once per file system:
`sudo sh bench/q3-space.sh /srv/moob/<fs> /srv/moob/<fs>/root <out dir>`

The script copies the data set inside the same file system, mounts a second layer over the copy,
deduplicates every file whose content equals an earlier one (whole file, `FIDEDUPERANGE` through
the layer), then makes a second copy of the tree through the layer with reflink and a third one
without. Bytes used are the file system's own count after `syncfs`.

| File | Content |
| --- | --- |
| `result-btrfs.txt`, `result-xfs.txt` | size profile, identical content, dedupe calls, bytes used at each step |

One run per file system; the machine was also running fstests in a guest at the time, which
does not affect byte counts.
