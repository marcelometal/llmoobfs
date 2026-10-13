# What a client pays after a query: content and attributes of the results

Journal [0072](../../../docs/history/0072-m3-phase-3c.md). Pilot-grade, warm cache, one host, **not
an M4 result**.

- Host: Mac mini (i7-2635QM), Debian 13, kernel 6.12.111, module of the series' tip; the project's
  own generated data set, domain `ns3` of the ext4 layer (7 459 files, about 2.8 KB and 7 typed
  attributes each).
- `sudo build/host/bench-readcost --dir /srv/moob/layer-ext4/ns3 --lower /srv/moob/ext4/root/ns3
  --attr kind --n N --repeat 500`, with `N` 20 (`n20.txt`) and 200 (`n200.txt`).

Rows are explained at the top of `bench/readcost.c`.
