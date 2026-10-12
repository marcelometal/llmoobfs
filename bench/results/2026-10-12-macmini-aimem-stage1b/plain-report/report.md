# LongMemEval-S retrieval — 2026-10-12T19:52:45.340157869Z

- commit: `385b81ea95f7b73ede7146d0ae606c4caa7b43c1`
- dataset: `longmemeval_s` (sha256 `08d8dad4be43ee20…`)
- hardware: Intel(R) Core(TM) i7-2635QM CPU @ 2.00GHz (8 threads)
- questions scored: 50 (0 abstention questions excluded)
- mode: zero-llm
- config: embeddings=none reranker=off

| slice | n | hit@1 | hit@3 | hit@5 | hit@10 | recall@1 | recall@3 | recall@5 | recall@10 | p50 ms | p95 ms | ctx tok mean | ctx tok med |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| overall | 50 | 0.600 | 0.640 | 0.680 | 0.700 | 0.600 | 0.640 | 0.680 | 0.700 | 14 | 36 | 322.9 | 327.5 |
| single-session-user | 50 | 0.600 | 0.640 | 0.680 | 0.700 | 0.600 | 0.640 | 0.680 | 0.700 | 14 | 36 | 322.9 | 327.5 |

Notes: hit@k = any evidence session in top k (the "Recall@k" most systems publish); recall@k = fraction of evidence sessions found. Latency is the `memory_query` MCP round trip (send→parse); ctx tok is the estimated context an agent ingests from the result = chars/4 over returned hit titles + snippets. Session attribution: `sessions/<id>.md` pages and raw observation hits; unattributable pages never score. Capture is production-shaped: excerpts bounded at the 2 KB privacy boundary.
