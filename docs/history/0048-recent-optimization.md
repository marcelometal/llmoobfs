# 0048 — Otimização do "N mais recentes" no `QUERY2`

Data: 2026-10-12. Estado: implementado, testado na VM da suíte e medido no host do registro 0042. Branch `m3/query-v2-recent`, sobre a da Q2d.

## Objetivo

O registro 0047 mostrou o SQLite à frente da v2 no "N mais recentes": 47 µs contra 185 µs com
`kind = note`, e 279 µs contra 1,7 ms com `agent = a07` (filtro que casa com cerca de 3% do
domínio). Pedido do mantenedor: otimizar.

## Diagnóstico

`perf` no host, com a consulta seletiva repetida 2 000 vezes: 47% em `inode_matches_all`
(percorre a lista de atributos de cada inode), 21% no laço de varredura, 13% em `rb_prev`, 9% em
`strcmp`. A leitura do código mostrou a causa principal, que o perfil só sugeria: sem
verificação por resultado, a varredura juntava candidatos em lotes de 256 mesmo quando o pedido
era de 20. Com um filtro de 3%, juntar 256 candidatos é percorrer praticamente o domínio inteiro
(cerca de 7 500 entradas) para usar 20. A estimativa do registro 0044 (cerca de 600 entradas a
2,8 µs) estava errada: eram cerca de 7 500 entradas a cerca de 0,23 µs.

## Mudanças (`layer/index.c`)

1. **Lote limitado ao que falta:** fora do modo de contingência, cada lote junta no máximo os
   resultados que faltam mais um (esse um só serve para marcar `truncated`). Vale para os três
   planos (varredura simples, ordenada e de igualdade). No modo de contingência o lote continua
   com 256, porque candidatos podem ser recusados pela verificação de permissão.
2. **Atributo por nó de nome:** dentro de um domínio, todos os candidatos estão na partição
   homogênea, então cada termo corresponde a um nó de nome; a busca na lista do inode compara
   ponteiros em vez de `strcmp` (`entry_find_node`). A v1, que percorre todas as partições,
   continua comparando nomes.
3. **Chave de retomada copiada uma vez por lote**, da última entrada examinada, em vez de a cada
   entrada.

Alternativa considerada e não feita: escolher, para consultas ordenadas com um termo de
igualdade seletivo, os N melhores da sequência desse termo com uma pequena pilha (heap). Com a
mudança 1, a varredura ordenada já ficou abaixo do SQLite; o plano extra fica para quando um caso
medido pedir.

## Resultados (p50, mesmas respostas; duas passadas por módulo)

| Forma no domínio ns3 | Antes (registro 0044) | Depois | SQLite (registro 0047) |
| --- | --- | --- | --- |
| 20 mais recentes, `kind = note` | 189 / 183 µs | 19 / 26 µs | 47 µs |
| 20 mais recentes, `agent = a07` | 1 779 / 1 727 µs | 102 / 141 µs | 279 µs |
| namespace e agent (253) | 166 / 166 µs | 119 / 144 µs | 178 a 476 µs |
| agent e kind (98) | 115 / 114 µs | 82 / 101 µs | 509 µs |
| ns1: namespace, kind e created (1 021) | 695 / 698 µs | 569 / 644 µs | 1 729 µs |

O "N mais recentes" ficou de 7 a 17 vezes mais rápido e passou à frente do SQLite nas duas
formas (cerca de 2 a 2,5 vezes). As outras formas ganharam de 10% a 30% com a comparação por
ponteiro.

## Evidências

- Host: respostas ordenadas (20 primeiros, nos dois sentidos) e paginação completa com limite 7
  (inclusive `kind = note`, com mais de 400 páginas), e paginação sem ordem com limite 50, iguais
  ao `moobfs-index --domain`: 14 de 14. `q2c-compare.sh` também confirmou respostas idênticas
  entre os módulos.
- VM da suíte (`make vm-test`): `QUERY2_ORDER_PASS`, `QUERY2_MISSING_BATCH_PASS`,
  `QUERY2_LEAPFROG_PASS` e `QUERY2_PASS` em ext4, btrfs e XFS, e todos os marcadores anteriores
  até `MOOBFS_LAYER_PASS`. KASAN: pelo CI no pull request.

## Limitações

- A comparação por ponteiro supõe que todas as entradas de um inode estão na partição dele; isso
  vale salvo depois de uma falha de memória ao mudar a partição, caso em que o índice já fica
  marcado como incompleto.
- Medido só em ext4, com o cache quente e sem escrita concorrente.
