# 0043 — Canal de tempo: `QUERY2` por domínio contra a v1 (teste 4 da especificação)

Data: 2026-10-12. Estado: medido no host do registro 0042; resultados em
`bench/results/2026-10-12-macmini-timing-v2/`.

## Objetivo

Verificar o teste 4 de `docs/spec-query-v2.md`, que faltava para o critério de pronto da Q2a: a
latência de uma consulta num domínio com 10 arquivos não deve variar além do ruído quando outro
domínio vai de 0 a 20 000 arquivos que casam com o predicado. E repetir, no mesmo host, o
experimento da v1 do registro 0030 como contraste.

## Método

`bench/timing-v2.sh` (uid 1001 com 10 arquivos em `dA`, uid 1002 com N arquivos em `dB`, ambos
0700; o layer é remontado depois de criar os diretórios, para não deixar os domínios em
fallback). Para o contraste, o mesmo conjunto com N = 20 000, remontado com `domain_depth=0`, e
a v1 sobre a raiz como uid 1001.

## Resultados (p50 por chamada, 10 resultados em todas)

| Caso | N (do uid 1002) | p50 | p99 |
| --- | --- | --- | --- |
| `QUERY2` | 0 | 15,3 µs | 17,5 µs |
| `QUERY2` | 1 000 | 23,4 µs | 30,3 µs |
| `QUERY2` | 5 000 | 16,3 µs | 25,1 µs |
| `QUERY2` | 20 000 | 14,0 µs | 15,9 µs |
| `QUERY2`, 3 repetições | 20 000 | 10,1 µs (10,0 a 12,5) | 14,5 µs |
| v1 como uid 1001, 3 repetições | 20 000 | 13,8 ms (13,79 a 13,81) | 16,5 ms |

## Conclusão

- **O canal fechou na v2.** Não há tendência com N: o maior N teve a menor latência da
  varredura, e a variação entre valores de N (10 a 23 µs) é da mesma ordem da variação entre
  repetições com N fixo. A v2 não percorre entradas de outros domínios, então o tempo não
  carrega informação sobre eles.
- **A v1 mantém o canal neste host:** 13,8 ms com 20 000 candidatos invisíveis, cerca de
  0,69 µs por candidato (na Azure, 1,15 µs; outra CPU). A v1 só fica aberta a usuários comuns
  com `domain_depth=0`, e com domínios ligados exige `CAP_SYS_ADMIN` (Q2a).
- Para a mesma pergunta com 20 000 arquivos alheios, a v2 respondeu cerca de 1 400 vezes mais
  rápido que a v1.

## Limitações

- Uma repetição por N na varredura; o ponto N = 1 000 (23,4 µs) é o mais alto e ficou isolado.
  A dispersão entre repetições foi medida só em N = 20 000, e o relatório marcou essa linha como
  inconclusiva pelo critério de 20% de spread, natural em valores de 10 µs. A conclusão sobre o
  canal não depende disso: a diferença para a v1 é de três ordens de grandeza.
- Só ext4; um único predicado de igualdade; sem carga concorrente.

## Próximo passo

Base da v1 e W3 por domínio neste host (formas do registro 0029), "N mais recentes" e Q2c
contra Q2b.
