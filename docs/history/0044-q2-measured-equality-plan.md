# 0044 — `QUERY2` medido contra a v1 e revisão do plano de igualdade da Q2c

Data: 2026-10-12. Estado: medido no host do registro 0042; plano revisado e testado na VM da suíte
e no host; KASAN pelo CI no pull request. Resultados brutos em `bench/results/2026-10-12-macmini-q2/`.

## Objetivo

Medir a v2 contra a v1 em escala no mesmo hardware (critério das fases Q2a a Q2c) e comparar o
plano da Q2c (interseção por avanço alternado, registro 0037) com o da Q2b.

## Método

60 000 memórias, perfil 8, semente 1 (as do registro 0029), gravadas com W1 sob a raiz do layer,
de modo que `ns0` a `ns7` são domínios. Três repetições de 50 chamadas por forma.
`bench/q2-campaign.sh` mede a v1 e a v2; `bench/q2c-compare.sh` recarrega módulos diferentes
sobre os mesmos dados e mede as mesmas formas da v2 com cada um, conferindo que as respostas são
idênticas.

## Resultados: v1 contra v2 (p50, mesmas respostas)

| Pergunta | v1 como root | v1 como usuário | v2 no domínio, como usuário |
| --- | --- | --- | --- |
| Q3: `namespace = ns3 AND agent = a07` (253) | 3,86 ms | 4,14 ms | 0,13 a 0,16 ms (plano revisado) |
| Q4: `namespace = ns1 AND kind = fact AND created >= T` (1 021) | 5,14 ms | 5,14 ms | 0,71 a 0,72 ms (plano revisado) |

As respostas da v1 e da v2 têm os mesmos inodes nas duas formas (`equivalence.txt`). A v1 como
usuário custa pouco mais que como root graças à revalidação RCU (registro 0028). A v2 não
percorre os outros domínios: Q3 ficou de 24 a 30 vezes mais rápida, Q4 cerca de 7 vezes.
Na Azure (registro 0029), Q3 e Q4 da v1 custavam 6,95 ms e 6,99 ms; este host é mais rápido por
consulta, então as comparações valem dentro dele.

## Resultados: planos de igualdade (p50 em µs, duas passadas por módulo)

| Forma no domínio | Q2b: varre o 1º termo | Q2c: avanço alternado | Revisado: sequência mais curta |
| --- | --- | --- | --- |
| namespace (todo o domínio) AND agent (253) | 2 311 / 2 320 | 411 / 424 | 127 / 162 |
| agent AND namespace | 112 / 109 | 289 / 304 | 127 / 160 |
| agent (253) AND kind (3 030), 98 resultados | 60 / 60 | 216 / 229 | 88 / 114 |
| namespace AND kind AND created (ns1), 1 021 | 2 335 / 2 397 | 1 549 / 1 550 | 710 / 723 |
| agent sozinho (mesmo plano nos três) | 71 / 71 | 81 / 86 | 79 / 98 |

A linha "agent sozinho" usa o mesmo código nos três módulos; a diferença entre elas (71 a 98 µs)
mostra a ordem do ruído desta medição.

## Decisão: o plano da Q2c foi trocado

O avanço alternado faz uma busca na árvore por passo. Ele só ganhou quando a sequência do
primeiro termo era longa (o caso `namespace` dentro do próprio domínio, em que o termo casa com
tudo); quando o primeiro termo já tinha sequência curta, foi 2,7 a 3,6 vezes mais lento que a
varredura simples da Q2b, porque cada passo custa uma busca logarítmica e a varredura custa um
`rb_next`. Com dados sem agrupamento, o número de passos do avanço alternado é da ordem da
sequência mais curta, então ele não reduz o trabalho em relação a varrer essa sequência.

Plano revisado: uma vez por chamada, as sequências de todos os termos `EQ` são percorridas em
paralelo a partir do inode de retomada até a primeira terminar; essa é a mais curta, a um custo
proporcional a ela. Depois só ela é varrida, e todos os termos são testados em cada inode. O
resultado ficou perto da melhor ordem escrita à mão em todas as formas: até 1,2 a 1,9 vez mais
lento que ela (o custo de medir as sequências), 3 vezes mais rápido que o avanço alternado no
melhor caso dele e cerca de 16 vezes mais rápido que a Q2b quando o primeiro termo é amplo.
Alternativa registrada: árvore aumentada com tamanhos de subárvore, que daria o tamanho de cada
sequência em tempo logarítmico; descartada por ora porque o percurso em paralelo já é barato e
não muda a estrutura do índice.

Consequência no cursor: como a sequência escolhida pode mudar entre páginas, a retomada desse
plano usa só o inode (as sequências estão todas em ordem de inode); o valor guardado no cursor é
ignorado nesse plano. A UAPI não muda. `docs/spec-query-v2.md` §4.4 recebe a revisão; o registro
0037 recebe nota apontando para este.

## Outras observações

- "N mais recentes" (ordem por `created` decrescente, 20 resultados) custou cerca de 185 µs com
  `kind = note` (cerca de 40% do domínio) e cerca de 1,7 ms com `agent = a07` (cerca de 3%): a
  varredura percorre cerca de 600 entradas da ordem até achar 20, a cerca de 2,8 µs por entrada.
  O custo por entrada é dominado pela busca do atributo na lista do inode (`entry_find`, uma
  alocação por atributo). Uma representação compacta dos atributos por inode é a otimização
  candidata; não foi feita.
- Remontagem com o cache quente: cerca de 0,71 s para 60 000 memórias (cerca de 12 µs por
  memória). Índice: 437 882 entradas, 36,4 MB (cerca de 607 bytes por memória).
- O `bench-load` contava toda resposta de "N mais recentes" como erro por estar truncada no
  limite; corrigido (só conta como estouro uma resposta sem ordem e com o limite máximo).

## Evidências

`make vm-test` com o plano revisado: `QUERY2_LEAPFROG_PASS` (o bloco da Q2c, inclusive a
verificação do plano pela ordem da resposta), `QUERY2_MISSING_BATCH_PASS`, `QUERY2_ORDER_PASS` e
`QUERY2_PASS` em ext4, btrfs e XFS, e todos os marcadores anteriores até `MOOBFS_LAYER_PASS`. No
host, `bench/q2c-compare.sh` confirmou respostas idênticas entre Q2b, avanço alternado e plano
revisado, cada um carregado duas vezes (`Q2C_COMPARE_PASS`).

## Limitações

- Só ext4 e um domínio por forma; sem carga concorrente.
- Ruído de até cerca de 25% entre passadas no mesmo host para valores de dezenas de µs.
- A comparação entre planos é sobre um conjunto sem agrupamento; em dados agrupados por inode o
  avanço alternado poderia ganhar, e isso não foi medido.

## Nota de correção (2026-10-12)

A explicação do custo do "N mais recentes" acima (cerca de 600 entradas a cerca de 2,8 µs por
entrada, atribuída à busca do atributo na lista do inode) estava errada. A causa principal era a
varredura juntar 256 candidatos mesmo para um pedido de 20, o que percorria cerca de 7 500
entradas a cerca de 0,23 µs. Diagnóstico, correção e novas medições no registro 0048.
