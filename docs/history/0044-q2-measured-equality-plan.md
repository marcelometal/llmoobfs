# 0044 — `QUERY2` measured against v1 and revision of the Q2c equality plan

Date: 2026-10-12. Status: measured on the host of journal 0042; plan revised and tested in the suite's VM
and on the host; KASAN by CI on the pull request. Raw results in `bench/results/2026-10-12-macmini-q2/`.

## Goal

Measure v2 against v1 at scale on the same hardware (criterion of phases Q2a to Q2c) and compare the
Q2c plan (intersection by leapfrog, journal 0037) with the Q2b one.

## Method

60 000 memories, profile 8, seed 1 (those of journal 0029), written with W1 under the layer root,
so that `ns0` to `ns7` are domains. Three repetitions of 50 calls per form.
`bench/q2-campaign.sh` measures v1 and v2; `bench/q2c-compare.sh` reloads different modules
over the same data and measures the same v2 forms with each one, checking that the responses are
identical.

## Results: v1 against v2 (p50, same responses)

| Question | v1 as root | v1 as user | v2 in the domain, as user |
| --- | --- | --- | --- |
| Q3: `namespace = ns3 AND agent = a07` (253) | 3.86 ms | 4.14 ms | 0.13 to 0.16 ms (revised plan) |
| Q4: `namespace = ns1 AND kind = fact AND created >= T` (1 021) | 5.14 ms | 5.14 ms | 0.71 to 0.72 ms (revised plan) |

The v1 and v2 responses have the same inodes in both forms (`equivalence.txt`). v1 as
user costs little more than as root thanks to RCU revalidation (journal 0028). v2 does not
walk the other domains: Q3 became 24 to 30 times faster, Q4 about 7 times.
On Azure (journal 0029), v1's Q3 and Q4 cost 6.95 ms and 6.99 ms; this host is faster per
query, so the comparisons hold within it.

## Results: equality plans (p50 in µs, two passes per module)

| Form in the domain | Q2b: scans the 1st term | Q2c: leapfrog | Revised: shortest run |
| --- | --- | --- | --- |
| namespace (the whole domain) AND agent (253) | 2 311 / 2 320 | 411 / 424 | 127 / 162 |
| agent AND namespace | 112 / 109 | 289 / 304 | 127 / 160 |
| agent (253) AND kind (3 030), 98 results | 60 / 60 | 216 / 229 | 88 / 114 |
| namespace AND kind AND created (ns1), 1 021 | 2 335 / 2 397 | 1 549 / 1 550 | 710 / 723 |
| agent alone (same plan in all three) | 71 / 71 | 81 / 86 | 79 / 98 |

The "agent alone" line uses the same code in the three modules; the difference between them (71 to 98 µs)
shows the order of the noise of this measurement.

## Decision: the Q2c plan was replaced

The leapfrog does one tree search per step. It only won when the run of the
first term was long (the `namespace` case within its own domain, where the term matches
everything); when the first term already had a short run, it was 2.7 to 3.6 times slower than the
simple Q2b scan, because each step costs a logarithmic search and the scan costs one
`rb_next`. With unclustered data, the number of leapfrog steps is of the order of the
shortest run, so it does not reduce the work compared to scanning that run.

Revised plan: once per call, the runs of all `EQ` terms are walked in
parallel from the resume inode until the first one ends; that is the shortest, at a cost
proportional to it. Then only it is scanned, and all terms are tested on each inode. The
result came close to the best hand-written order in all forms: up to 1.2 to 1.9 times
slower than it (the cost of measuring the runs), 3 times faster than the leapfrog in its
best case and about 16 times faster than Q2b when the first term is broad.
Alternative recorded: an augmented tree with subtree sizes, which would give the size of each
run in logarithmic time; discarded for now because the parallel walk is already cheap and
does not change the structure of the index.

Consequence for the cursor: since the chosen run can change between pages, resuming this
plan uses only the inode (the runs are all in inode order); the value stored in the cursor is
ignored in this plan. The UAPI does not change. `docs/spec-query-v2.md` §4.4 receives the revision; journal
0037 receives a note pointing to this one.

## Other observations

- "N most recent" (ordered by `created` descending, 20 results) cost about 185 µs with
  `kind = note` (about 40% of the domain) and about 1.7 ms with `agent = a07` (about 3%): the
  scan walks about 600 entries of the order until it finds 20, at about 2.8 µs per entry.
  The per-entry cost is dominated by the search for the attribute in the inode's list (`entry_find`, one
  allocation per attribute). A compact per-inode representation of the attributes is the candidate
  optimization; it was not done.
- Remount with a warm cache: about 0.71 s for 60 000 memories (about 12 µs per
  memory). Index: 437 882 entries, 36.4 MB (about 607 bytes per memory).
- `bench-load` counted every "N most recent" response as an error because it was truncated at the
  limit; fixed (only a response without ordering and with the maximum limit counts as overflow).

## Evidence

`make vm-test` with the revised plan: `QUERY2_LEAPFROG_PASS` (the Q2c block, including the
plan verification by the order of the response), `QUERY2_MISSING_BATCH_PASS`, `QUERY2_ORDER_PASS` and
`QUERY2_PASS` on ext4, btrfs and XFS, and all the previous markers up to `MOOBFS_LAYER_PASS`. On the
host, `bench/q2c-compare.sh` confirmed identical responses between Q2b, leapfrog and the revised
plan, each loaded twice (`Q2C_COMPARE_PASS`).

## Limitations

- Only ext4 and one domain per form; no concurrent load.
- Noise of up to about 25% between passes on the same host for values of tens of µs.
- The comparison between plans is over an unclustered set; on data clustered by inode the
  leapfrog could win, and this was not measured.

## Correction note (2026-10-12)

The explanation of the cost of "N most recent" above (about 600 entries at about 2.8 µs per
entry, attributed to the search for the attribute in the inode's list) was wrong. The main cause was the
scan gathering 256 candidates even for a request of 20, which walked about 7 500
entries at about 0.23 µs. Diagnosis, fix and new measurements in journal 0048.
