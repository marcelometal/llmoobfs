# G5 hybrid-retrieval gate, 2026-10-25 to 2026-10-26 (journal 0201)

Raw output of `bench/client/g5_mounted.py` on the dedicated Debian host
(`environment.txt`), ext4 lower, `domain_depth=1`, `index_build_work=65536`,
module built from the layer at `c793f8d` with `make layer`. Packages, model
and dataset revisions are pinned in `environment.txt`. The bounds are
protocol section 13.6.

| File | What it is |
| --- | --- |
| `gate.log`, `gate/` | the gate run; it printed `G5_MOUNTED_PASS` |
| `gate/gate.json` | every `G5_*` record of the run |
| `gate/gate-raw.json.gz` | the oracle's, the plain-files path's and the three MoobFS runs' results and timings, per uid and query |
| `gate/relevance.json` | Q1 relevance per question and branch, as the gate scored it |
| `gate/sample.json` | the drawn sample, in order, with type and domain |
| `gate/report.json`, `gate/report.log` | recorded, unbounded numbers of section 13.7 (Q2, per type), from `g5_report.py` |
| `smoke.log`, `smoke/` | harness check on 6 instances before the gate; not a gate run |
| `attempt1-power.log`, `attempt1-power/` | lost to a power outage during index build; nothing measured |
| `attempt2-e2big.log`, `attempt2-e2big/` | `QUERY2` refused the private domains with `E2BIG` under the default `index_build_work`; fixed in the harness |
| `g4-rerun.*` | the G4 gate rerun after the shared watcher change; printed `G4_MOUNTED_PASS` |

Commands, as root on the host:

```text
python bench/client/g5_mounted.py --ko layer/moobfs_layer.ko --out-dir gate
python bench/client/g5_report.py --gate-dir gate
```

`gate.json` as written by the gate holds the raw results too; here they are
split into `gate-raw.json.gz` to keep the file small, and the rest is
indented. No value was changed.
