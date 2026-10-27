# QUERY2 over a slow lower device, 2026-10-27 (issue 118, journal 0210)

`bench/client/issue118_slow_lower.py` on the dedicated Debian host (kernel `6.12.111+deb13-amd64`,
Intel Core i7-2635QM), layer module built with `make layer` from the branch of journal 0210,
default `index_build_work` (32 768) and the ten-second cooperative build deadline. The lower is
ext4 on a loop device behind `dm-delay` (Debian's module), 5 000 files in one domain owned by
uid 2001, 2 500 of them matching `kind == note`. For each delay the layer was mounted afresh
and the page cache dropped, then uid 2001 queried twice.

| Delay per I/O | First QUERY2 (builds the domain) | Second QUERY2 |
| --- | --- | --- |
| 0 ms | 2 500 results, 0.30 s | 2 500, 0.01 s |
| 5 ms | 2 500, 0.81 s | 2 500, 0.01 s |
| 20 ms | 2 500, 1.37 s | 2 500, 0.01 s |
| 50 ms | 2 500, 3.05 s | 2 500, 0.01 s |
| 100 ms | 2 500, 5.25 s | 2 500, 0.01 s |
| 200 ms | 2 500, 9.76 s | 2 500, 0.01 s |
| 400 ms | `ETIMEDOUT` after 11.07 s, no result list | 2 500, 8.46 s (pages cached by the first attempt) |
| 0 ms, after the 400 ms run | 2 500, 0.04 s | — |

`dmesg` showed only `building the index of a domain failed (-110)` for the 400 ms build, and no
kernel warning. Two invocations: `DELAYS="0 5 20 50"`, then `DELAYS="100 200 400"`.
