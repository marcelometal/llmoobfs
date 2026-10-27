# G1 gate on the Python library's store, 2026-10-27 (journal 0207)

Raw output of `bench/client/g1_mounted.py --store package` on the dedicated Debian host
(`environment.txt`), ext4 lower, `domain_depth=1`, module built with `make layer` from the
branch of journal 0207 (the layer is unchanged since `c793f8d`). Test process: a fresh venv with
the G1 venv's packages (`pip freeze` of it: langgraph 1.2.15, langgraph-checkpoint 4.3.0) and the
library installed with `pip install --no-build-isolation`, setuptools 84.0.0 by hash.

| File | What it is |
| --- | --- |
| `run1.log`, `run2.log`, `run3.log` | three complete runs; each printed `G1_MOUNTED_PASS` |

The fallback case follows the amendment of protocol section 5 dated 2026-10-27: without the
module, `salary.json` is absent for uid 1001 and uid 1002. Command, as root:

```text
python bench/client/g1_mounted.py --store package --ko layer/moobfs_layer.ko \
  --lower /srv/moob/ext4/g1-c5-lower --mnt /mnt/g1-c5 --budget-lower /srv/moob/ext4/g1-c5-budget \
  --budget-mnt /mnt/g1-c5-budget --plain /srv/moob/ext4/g1-c5-plain
```
