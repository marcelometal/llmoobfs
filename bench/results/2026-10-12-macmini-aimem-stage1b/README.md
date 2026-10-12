# ai-memory stage 1b on the Mac mini (2026-10-12)

ai-memory v2.6.3 (385b81ea) LongMemEval-S retrieval harness, zero-LLM mode (no embeddings, no
reranker), 50 questions, a real `ai-memory serve` with its data directory on plain ext4 and inside
the layer over ext4 on the same SSD (`bench/client/stage1b-retrieval.sh`). Kernel 6.12.111, module
of `fix/rename-over-deleted-name` (e82ab64). One run per side; nothing else ran on the host.

| | plain ext4 | layer over ext4 |
|---|---:|---:|
| hit@1 / @3 / @5 / @10 | 0.600 / 0.640 / 0.680 / 0.700 | 0.600 / 0.640 / 0.680 / 0.700 |
| per-question results (sessions, hit, recall, context) | — | identical for all 50 |
| `memory_query` p50 / p95 (harness) | 14 / 36 ms | 15 / 45 ms |
| `memory_query` mean | 16.2 ms | 18.7 ms |
| whole run (replay + queries) | 460 s | 505 s |

Accuracy is the property that must hold: the layer changes nothing in what the client finds. The
latency difference (+1 ms at p50, about +15 % in mean and in wall time) is from one run of 50
questions and is not yet a measured overhead; repeated runs and the full 500 questions are needed
before quoting it. `*-report/` are the harness reports as produced.

An earlier attempt on the module of `main` was stopped before its layer half (the rename bug of
journal 0057 affected that module); it is not used.
