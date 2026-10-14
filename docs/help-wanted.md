# How to help

MoobFS is an experimental kernel module with one maintainer and no outside review yet. This
page says what would help most, by how much kernel knowledge it takes. Read the status block at
the top of the [README](../README.md) first: nothing here is ready for production, and the
project says where it loses as readily as where it wins.

Every item below is small and bounded on purpose. A finding of any size is recorded in the
project's journal with credit, if the person wants it.

## If you know Linux filesystems or the VFS

| What | Where |
| --- | --- |
| Review the credential model: name operations with the caller's credentials, data operations on an open file with the mounter's, checked against a table. Not the overlayfs model, which checks both and runs every real operation as the mounter ([journal 0099](history/0099-credential-model-corrected.md)) | [issue 16](https://github.com/studiomoob/llmoobfs/issues/16) |
| Review the locking of the attribute index against rename, unlink, eviction and memory reclaim | [issue 17](https://github.com/studiomoob/llmoobfs/issues/17) |
| Tell us what a stacked filesystem usually gets wrong that our tests have not caught. This week's own list is in journals [0069](history/0069-setattr-crash-and-mounter-capability.md), [0074](history/0074-privileges-kept-on-copy.md), [0078](history/0078-syncfs-reaches-the-lower.md), [0082](history/0082-index-lock-and-reclaim.md) and [0083](history/0083-index-inspection-fixes.md) | open an issue |
| Run the suite under a Linux security module policy (SELinux has never been loaded in a test) | [testing.md](testing.md) |

## If you build memory for agents

| What | Where |
| --- | --- |
| Check whether your system fits: one file per memory, fields as typed attributes, scopes as directories | [client-contract.md](client-contract.md), section 1 |
| Tell us which of your structured queries do not fit four ANDed comparisons with one ordering | [research-client-query-patterns.md](research-client-query-patterns.md) |
| Try an adapter. The second client being written is the LangGraph store; the first was ai-memory, unmodified | [journal 0081](history/0081-second-client-and-outside-review.md) |

## If you want to try it or test it

| What | Where |
| --- | --- |
| Run the tests in a VM (Docker only): `make vm-test`; a subset: `MOOB_ONLY="btrfs reserve" make vm-test` | [testing.md](testing.md) |
| Try another kernel than 6.12 and report what breaks | [compatibility.md](compatibility.md) |
| Repeat a measurement on your hardware; the scripts and raw results are in `bench/` | [bench/results/](../bench/results/README.md) |

## If you do not touch kernels

The issues labelled [good first issue](https://github.com/studiomoob/llmoobfs/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22)
need none: manual pages, documented exit codes, a compiler warning matrix, an accessibility
audit of the site, an SPDX check.

## How to report

- A defect that an ordinary user could use against others, or that crashes the kernel: privately,
  through [SECURITY.md](../SECURITY.md).
- Anything else: an issue. For a review, the `Review request` template keeps the answer short.
- Rules for changes: [CONTRIBUTING.md](../CONTRIBUTING.md). Kernel changes need the VM suite and the
  KASAN job green; commits are signed off (DCO).
