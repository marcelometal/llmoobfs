# Client contract: what a memory system needs to use the layer

Date: 2026-10-13. Status: **convention** (M3 phase 3a, journal [0070](history/0070-client-contract.md)).
It gathers in one place what the specifications already decided, so that any agent-memory system
can be checked against it without reading a particular client's test plan. It names no client.
Each rule says where it comes from and whether it is enforced by the kernel, checked by a tool, or
only a convention the client keeps. One section is marked **proposal**: it is not decided.

Sources: [spec-m2](spec-m2.md), [spec-m3](spec-m3.md) §2, [spec-query-v2](spec-query-v2.md),
[ADR-0011](decisions/0011-threat-and-credential-model.md), `include/moobfs_attr.h`,
`include/moobfs_ioctl.h`.

## 1. Does a system fit?

| Question | Needed | If not |
| --- | --- | --- |
| Is each memory a regular file in a directory tree? | yes | out of scope: the layer indexes files, it is not a database |
| Can the structured fields be written as extended attributes of that file? | yes | nothing to query |
| Does every scope the system isolates map to a directory at one fixed depth? | yes | scopes at mixed depths need one mount per depth, or `domain_depth=0` with its limits (§2) |
| Do the structured queries fit a conjunction of at most four comparisons with one ordering? | mostly | the rest stays in the client's own store (§8) |
| Does it run on Linux with a kernel of the supported list, on a host where a module can be loaded? | for the layer | the client keeps working without it (§7) |

## 2. Scope is a directory

- The administrator mounts with `domain_depth=N`. Every directory exactly `N` levels below the
  mount root is a **domain**; the index has one partition per domain. *Kernel.*
- A query (`MOOB_IOC_QUERY2`) is issued on an open descriptor of the domain directory and answers
  from that domain only. The caller needs search and read permission on that directory. Results
  are paths relative to it. *Kernel.*
- Files above depth `N` belong to no domain and are not returned by `QUERY2`. *Kernel.*
- Moving a **file** to another domain moves its entries. Moving a **directory** across domains
  fails with `EXDEV`, as across filesystems; copy and delete instead. *Kernel.*
- `domain_depth=0` turns domains off: only the older `QUERY` on the mount root exists, with a
  permission check per result. It is an administrator's choice with a documented timing channel
  (ADR-0011) and not the mode this contract describes.

The client therefore chooses its directory layout so that the unit it isolates (a user, an agent,
a project, a tenant) is one directory at depth `N`, and tells the administrator which `N`.

## 3. Permissions: one owner per domain

A file is in its domain's partition only if every process that can enter the domain directory can
also read the file (the homogeneity rule, spec-query-v2 §3). A file that fails it is silently
absent from answers. *Kernel.*

The arrangement that always satisfies the rule, and the one to deploy:

- the domain directory, every directory below it and every file have the same owner and group;
- directories and files grant to each class no less than the domain directory grants: with a
  private domain, directories `0700` and files `0600` (a process `umask` of `077`);
- no access ACLs on files, no hard links (a file with two names is left out).

Changing the mode, owner or ACL of a directory at or below the domain depth is allowed, but that
domain then answers with a permission check per result until the next mount (slower, still
correct). Clients should set permissions when they create a domain and leave them.

## 4. Structured fields are typed attributes

- Name: `user.moob.<name>`, `<name>` of 1 to 64 bytes from `[a-z0-9_.]`. *Kernel, on writes
  through the layer.*
- Value: one type byte, then the payload, little-endian. *Kernel validates every write.*

| Type | Byte | Payload | Indexed |
| --- | --- | --- | --- |
| string | 1 | UTF-8 without NUL, up to 1 024 bytes | yes |
| int64 | 2 | 8 bytes, two's complement | yes |
| double | 3 | 8 bytes, IEEE 754, not NaN | yes |
| bool | 4 | 1 byte, 0 or 1 | yes |
| time | 5 | 8 bytes, signed nanoseconds since the epoch | yes |
| raw | 6 | up to 4 096 opaque bytes | no |

- At most 64 typed attributes per file. *Kernel.*
- The kernel gives no meaning to any attribute name. Names are the client's vocabulary; two
  clients sharing a tree must agree on them between themselves.
- A field that is absent is different from a field of another type: queries can ask for "greater
  than X or absent" (§6), which is how an optional expiry is expressed.
- `moobfs-attr` sets and reads them from a shell; any language can use `setxattr` with the
  encoding above.

## 5. Identity and provenance

Identity is the client's, expressed in ordinary typed strings (spec-m3 §2). *Convention; one
offline check.*

| Attribute | Meaning |
| --- | --- |
| `user.moob.namespace` | logical scope of the identity; opaque |
| `user.moob.memory_id` | stable identity inside the namespace; a UUID v7 as text is recommended |
| `user.moob.agent` | who created or last updated it; authorship, not part of the key |
| `user.moob.origin_namespace`, `user.moob.origin_id` | where a copied memory came from |

- The key is `(namespace, memory_id)`. The kernel does not generate, check or interpret it.
- `rename` keeps the identity: attributes follow the inode.
- A copy or a reflink is a new inode. When it is a new memory, the client gives it a new
  `memory_id` and records the source in the two `origin_` attributes before the file gets its
  final name. A copy that keeps the key is a conflict.
- A hard link is the same inode and the same identity; it cannot carry a second one, and it takes
  the file out of `QUERY2` answers (§3).
- `moobfs-check <directory>` reports two different inodes with the same key as a problem (exit
  status 2), counts the same id in another namespace as normal, and ignores several paths to one
  inode. *Tool; tested in `vm/suite.sh`.* Comparing content to flag a copy without provenance is
  described in spec-m3 and not implemented.
- `namespace` and the domain directory are independent: the first is what the client calls a
  scope, the second is what the kernel isolates. Using one namespace per domain keeps them from
  disagreeing, and is recommended.

## 6. Writing and querying

**Write sequence** (spec-m2 §6). Content and attributes are separate operations, so a memory
is published in this order: write a temporary file in the same directory, set every attribute on
it, `fsync` it, `rename` it to the final name, `fsync` the directory. A file under its final name
then has all its content and attributes, and the name survives a power loss. *Convention; the
power-loss replay of `vm/suite.sh` tests it.* Changing several attributes of a published file is
not atomic as a group; rewrite the file when that matters.

**Query** (spec-query-v2 §4). One request holds up to four terms, all of which must hold:

- operators: equal, less, less or equal, greater, greater or equal, string prefix, exists;
- a term can also accept files that lack the attribute;
- optional ordering by one attribute, ascending or descending, with a limit of up to 4 096
  results per call and a cursor to continue;
- up to eight requests in one `MOOB_IOC_QUERY2_BATCH`, each with its own answer. "Attribute in a
  set" is one request per value; the union is the client's.

A result is an inode number and a path of up to 256 bytes relative to the domain; a longer path is
flagged as truncated. Answers reflect the index at the time of the call and are not a snapshot
across calls.

## 7. Detecting the layer and falling back (proposal)

Not decided; proposed in `adoption-risks.md` (risk 5) so that a client never requires the layer.

1. `statfs` on the data directory: file system type `0x4d4f4f42` means the layer is mounted there.
2. `MOOB_IOC_GET_INFO` on the mount root gives the UAPI version, `domain_depth` and the state
   flags. Use the layer for queries only when domains are on, the index is not off
   (`MOOB_FLAG_INDEX_OFF`) and not incomplete (`MOOB_FLAG_INDEX_INCOMPLETE`).
3. Otherwise answer from the client's own index, exactly as without the layer.

Open point: step 2 needs a descriptor of the mount root, which a client confined to its domain may
not be able to open. A way to learn the same state from the domain directory does not exist yet.

## 8. What stays in the client's own store

Full-text and vector search; joins, aggregates and OR across different attributes; history and
revisions (spec-m3 §5 keeps them in the client, with git or an attribute it maintains); records
that are not files. The layer removes the duplicated structured columns and their
synchronization; it does not replace the client's database.

## 9. A second client

Every real-client result so far comes from one system. Before the layer or its interface changes
for a client's convenience, the reason should hold for more than one of the systems surveyed in
`research-client-query-patterns.md`, and a second system should pass §1 and run the equality test
of `client-test-plan.md` stage 2. Client-specific handling belongs in that client's adapter.

Decided on 2026-10-13 (journal [0081](history/0081-second-client-and-outside-review.md)): the
second client is the **LangGraph store** (`BaseStore`), through an adapter in `bench/client/`.
It was chosen because it does not fit perfectly; the plan and the known mismatches are in that
journal.
