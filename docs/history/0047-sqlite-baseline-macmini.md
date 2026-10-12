# 0047 — `QUERY2` contra o SQLite no mesmo hardware

Data: 2026-10-12. Estado: medido no host do registro 0042; resultados em
`bench/results/2026-10-12-macmini-sqlite/` e `bench/results/2026-10-12-macmini-q2/`.

## Objetivo

A pergunta que os registros 0043 e 0044 deixaram aberta: com os ajustes da série Q2, como o
`QUERY2` se compara com a base SQLite do harness (alternativa B da ADR-0009), na mesma máquina e
nos mesmos dados. Na Azure (registro 0029), o SQLite respondia Q3 em 0,39 ms e a v1 em 6,95 ms,
mas eram outro host e outro código.

## Método

`bench/sqlite-baseline.sh` (novo): banco de `bench/sqlbase.sh` com os índices compostos do
harness, carregado dos mesmos 60 000 arquivos da campanha do registro 0044, no mesmo SSD;
consultas pela biblioteca (`bench-sqlclient`), 3 repetições de 50 execuções. As formas da v2
foram escritas em SQL com o domínio como a coluna `namespace` (`extra.sql`). Os dois lados
devolvem número de inode e caminho de cada resultado.

## Resultados (p50; v2 com o plano revisado do registro 0044)

| Pergunta | SQLite | `QUERY2` | Quem ganha |
| --- | --- | --- | --- |
| namespace = ns3 e agent = a07 (253) | 178 a 476 µs | 127 a 162 µs | v2, de 1,1 a 3,7 vezes |
| namespace = ns3, agent = a07 e kind = note (98) | 509 µs | 88 a 114 µs | v2, de 4,5 a 5,8 vezes |
| namespace = ns1, kind = fact e created ≥ T (1 021) | 1 729 µs | 710 a 723 µs | v2, cerca de 2,4 vezes |
| 20 mais recentes de ns3 com kind = note | 47 µs | 180 a 191 µs | SQLite, cerca de 4 vezes |
| 20 mais recentes de ns3 com agent = a07 | 279 µs | 1 638 a 1 801 µs | SQLite, cerca de 6 vezes |

A faixa do SQLite na primeira linha vem de duas execuções do mesmo comando SQL em posições
diferentes do ciclo: 476 µs logo depois de consultas que devolvem 24 000 e 6 000 linhas (cache da
CPU frio), 178 µs depois de uma consulta parecida. A v2 foi medida em ciclos de formas pequenas.
A comparação justa fica entre os dois extremos.

## Conclusões

- **Filtros de igualdade e intervalo dentro de um domínio:** a v2 ficou à frente do SQLite com
  índices compostos em todas as formas medidas. Isso inverte o quadro da v1, que perdia por 10 a
  18 vezes na Azure.
- **"N mais recentes":** o SQLite ganha. Ele tem um índice composto (`namespace, kind, created`)
  que entrega os 20 primeiros já em ordem, e no caso de `agent` combina índice e ordenação. A v2
  percorre a árvore de `created` do domínio e testa os termos em cada entrada, com cerca de 2,8 µs
  por entrada (registro 0044). É o principal ponto fraco restante, e a otimização candidata
  continua a mesma: atributos compactos por inode para o teste de termos.
- **Formas amplas** (dezenas de milhares de resultados): o SQLite devolve tudo numa chamada (Q1
  com 24 595 linhas em 46 ms); a v2 limita cada chamada a 4096 resultados e pagina por cursor.
  Não foi medido o custo de paginar uma resposta inteira dessas.
- **Custo de manter o banco:** a carga do SQLite a partir dos arquivos levou 4 s para 60 000
  memórias, mas um SQLite lateral precisa ficar sincronizado com cada escrita, o que a v2 faz no
  próprio caminho de escrita. Esse custo não entra nesta tabela; é o argumento da ADR-0009.

## Limitações

- Só ext4, um domínio por forma, sem carga concorrente nem escrita durante as consultas.
- A posição de cada forma no ciclo afeta o cache da CPU nos dois lados; os ciclos do SQLite e da
  v2 não eram iguais.
- O SQLite foi usado com os índices do harness, sem ajuste fino de `PRAGMA` além de WAL e
  `synchronous=NORMAL`.

## Próximo passo

Otimizar o teste de termos por inode (o gargalo do "N mais recentes") e medir de novo; medir a
paginação completa de uma forma ampla; repetir em btrfs e XFS.
