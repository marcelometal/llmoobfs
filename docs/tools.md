# Command-line tools: synopsis, diagnostics and exit status

The Linux tools that read and check typed attributes (`make attr-tools`; they use the Linux
xattr API and do not build on macOS). Both print their synopsis with `--help` (exit 0) and,
when called wrongly, on standard error (exit 64). Errors name the tool and the file:
`moobfs-attr: <file>: <reason>`, `moobfs-check: <directory>: <reason>`. Experimental, like the
rest of the project.

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
