# G4 derived-index gate, 2026-10-25 (journal 0195)

Raw output of `bench/client/g4_mounted.py` on the dedicated Debian host
(`environment.txt`), ext4 lower, `domain_depth=1`, module built from the
layer at `c793f8d` with `make layer`. Test process: the LangGraph venv of
the G1 gate (langgraph 1.2.15, langgraph-checkpoint 4.3.0). Watchers:
`/usr/bin/python3 -I`, standard library only.

| File | What it is |
| --- | --- |
| `run1.*`, `run2.*`, `run3.*` | the three runs protocol section 12.6 requires; each printed `G4_MOUNTED_PASS` |
| `attempt1-exec.log` | harness failure before any scenario: the watcher, run as uid 1001, could not execute the venv Python under the operator's home |
| `attempt2-served.*` | harness failure after F1 and the E3 inventories: a call without its uid argument; its F1 numbers are kept and are not one of the three runs |

`.json` holds every scenario's fields, the F1 latency lists in seconds and the
derived-search statistics. Command, as root:

```text
python bench/client/g4_mounted.py --ko layer/moobfs_layer.ko --label N --out runN.json
```
