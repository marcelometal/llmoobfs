# Command-line tools: synopsis, diagnostics and exit status

The Linux tools that query, read and check typed attributes (`make attr-tools`; they use the
Linux xattr API and do not build on macOS). All print their synopsis with `--help` (exit 0) and,
when called wrongly, on standard error (exit 64). Errors name the tool and the file:
`moobfs-attr: <file>: <reason>`, `moobfs-check: <directory>: <reason>`. Experimental, like the
rest of the project.

## moobfs

```
moobfs info [--json] <path>
moobfs query [--json] [--ino] [--order NAME TYPE asc|desc] [--limit N] [--page N] <dir> [terms...]
moobfs check [moobfs-check arguments]
moobfs --help
```

One command for a mounted layer (roadmap item C4, journal 0198), over the existing ioctls; it
adds no kernel interface. Errors start with `moobfs:` and say what to do.

- `info` finds the layer mount that holds `<path>` and prints its `GET_INFO`: lower filesystem,
  interface versions, whether the index is on, the domain depth, the index budget and build
  work, attribute limits, and for an administrator the index counters. `--json` prints one
  object. Through a bind mount of a subdirectory the mount root is not reachable, and `info`
  says so (client contract, section 7).
- `query` issues `QUERY2` on a domain directory and follows the cursor until the answer is
  complete or `--limit` results were printed; `--page` sets the results per call (default
  4096). On the root of a mount without domains it issues the mount-wide `QUERY`, which has no
  cursor. Terms are those of `moobfs-index`: `name op type value`, up to four, an operator
  ending in `?` also matching files without the attribute. Paths are relative to `<dir>`, one
  per line (`--ino` adds the inode number and a tab; a `?` marks a path the kernel did not
  know). Without `--order` the results are sorted by inode number, as `moobfs-index` prints
  them. A cursor made stale by the domain changing restarts the answer, at most three times;
  `EAGAIN` is retried three times.
- `check` runs `moobfs-check` from the same directory, else from `PATH`, with its arguments
  and exit status.

| Exit | Meaning |
| --- | --- |
| 0 | success |
| 1 | a system call failed (the reason is printed) |
| 3 | `query`: the mount-wide `QUERY` had more than 4096 results and cannot return the rest; those returned were printed |
| 64 | usage |
| 65 | invalid terms, or a query the kernel refused as invalid (`EINVAL`) |
| 69 | no layer there: the module is not loaded, `<path>` is on another filesystem, `<dir>` is not a domain directory (`ENOTTY`), or the index is off (`noindex`) |
| 75 | temporary: the domain does not fit `index_budget` (`ENOMEM`), too many first queries in a minute (`EDQUOT`), the build needs more than `index_build_work` (`E2BIG`) or took longer than its deadline (`ETIMEDOUT`), or the domain kept changing (`EAGAIN`) |
| 77 | permission denied (`EACCES`, or `EPERM` for the mount-wide `QUERY` of a user when domains are on) |

## moobfs-attr

```
moobfs-attr get <file> <name>
moobfs-attr set <file> <name> <string|int64|double|bool|time|raw> <value>
moobfs-attr rm <file> <name>
moobfs-attr list <file>
moobfs-attr rawset <file> <name> <hex>   (test helper: writes bytes unchecked)
moobfs-attr --help
```

`<name>` is written without the `user.moob.` prefix. On a layer mount the kernel validates every
write; on a plain directory the tool validates before writing (`rawset` does not, by design).
`get` and `list` print `<name>\t<type>\t<value>`, or `<name>\tinvalid\t<reason>` for a value that
does not decode.

| Exit | Meaning |
| --- | --- |
| 0 | success |
| 1 | a system call failed (the reason is printed: file missing, permission, not supported, an invalid value refused by the layer) |
| 2 | `get`: the attribute exists but its value is invalid (printed as `invalid`) |
| 3 | `get`: no such attribute |
| 64 | usage: wrong arguments, a name that is not valid, a bad hex string for `rawset` |
| 65 | `set`: the value cannot be encoded as the type, or is invalid for it (for example a NaN double or a string over 1024 bytes) |

## moobfs-check

```
moobfs-check [--domain-depth N] <directory>
moobfs-check --help
```

Validates every typed attribute under `<directory>` and the format marker on its root; never
repairs. Prints one JSON object (`format`, `problems`, and with `--domain-depth` the files left
out of `QUERY2` answers, `left_out`, which are not problems).

| Exit | Meaning |
| --- | --- |
| 0 | clean |
| 1 | the tree could not be walked (the reason is printed) |
| 2 | problems found (listed in `problems`) |
| 64 | usage, or `--domain-depth` outside 1..32 |
