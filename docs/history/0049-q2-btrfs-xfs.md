# 0049 — `QUERY2`, v1 e SQLite em btrfs e XFS

Data: 2026-10-12. Estado: medido no host do registro 0042; resultados em
`bench/results/2026-10-12-macmini-btrfs-xfs/`.

## Objetivo

Os registros 0044, 0047 e 0048 mediram só em ext4. Repetir a campanha da v1 contra a v2 e a
base SQLite nas partições btrfs e XFS do mesmo SSD, com o módulo da otimização do registro 0048.

## Resultados (p50; mesmas 60 000 memórias, perfil 8, semente 1)

| Pergunta | Sistema | ext4 | btrfs | XFS |
| --- | --- | --- | --- | --- |
| namespace = ns3 e agent = a07 (253) | v1 como usuário | 4,14 ms | 3,71 ms | 2,75 ms |
| | SQLite | 178 a 476 µs | 178 a 474 µs | 172 a 407 µs |
| | v2 | 119 a 144 µs | 145 µs | 145 µs |
| namespace, kind e created (1 021) | v1 como usuário | 5,14 ms | 4,35 ms | 3,32 ms |
| | SQLite | 1 729 µs | 1 719 µs | 1 867 µs |
| | v2 | 569 a 644 µs | 684 µs | 697 µs |
| 20 mais recentes, kind = note | SQLite | 47 µs | 47 µs | 68 µs |
| | v2 | 19 a 26 µs | 24 µs | 17 µs |
| 20 mais recentes, agent = a07 | SQLite | 279 µs | 273 µs | 278 µs |
| | v2 | 102 a 141 µs | 122 µs | 90 µs |

Os valores de ext4 da v2 são os do registro 0048 (duas passadas); os de btrfs e XFS, de uma
campanha de 3 repetições cada. A v1 e a v2 devolveram os mesmos inodes nas duas formas
comparáveis nos três sistemas de arquivos.

## Conclusões

- A v2 ficou à frente do SQLite em todas as formas medidas, nos três sistemas de arquivos.
- A v2 praticamente não depende do sistema de arquivos inferior: o índice está na RAM e, sem
  verificação por resultado, nada é lido do disco. A v1 depende (2,7 a 4,1 ms em Q3), porque
  resolve o caminho de cada resultado no inferior.
- O SQLite também quase não varia, porque o banco inteiro cabe no cache.

## Limitações

- Uma campanha por sistema de arquivos em btrfs e XFS (três repetições); ruído da ordem de 20%
  em valores de dezenas de µs.
- Cache quente, sem escrita concorrente; a criação das 60 000 memórias (W1) não foi analisada
  aqui.
