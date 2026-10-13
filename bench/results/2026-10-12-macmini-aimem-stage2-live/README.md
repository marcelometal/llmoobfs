# Stage 2 of the real-client test: live mirror, and the scale repeated on the current module

Journal [0071](../../../docs/history/0071-client-stage2-live-mirror.md). Pilot-grade, **not an M4
result**. Same host as the scale campaign; module built from the tip of the series (journals 0061
and 0069 included).

- `live-10x200/`: `sudo LIVE=1 sh bench/client/stage2.sh OUT 10 200`. `sidecar.txt` is the live
  mirror's summary (the two lines around it come from `su` ending the session), `verify.txt` the
  comparison of the attributes with the client's database, `reconcile-lines.txt` the number of
  reconciliation lines in the server log, `write-cost.txt` the client's time per page per project.
- `bulk-10x2000-module-0069/`: `sudo BULK=1 sh bench/client/stage2.sh OUT 10 2000`, to compare
  with `bulk-10x2000/` of the scale campaign, measured on the previous module.
