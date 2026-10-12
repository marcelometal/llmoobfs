# 0037 — Fase Q2c: interseção por avanço alternado (leapfrog) no `QUERY2`

Data: 2026-10-11. Estado: implementado e testado na VM QEMU da suíte (kernel Debian 6.12.111,
ext4, btrfs e XFS); KASAN pelo CI no pull request; medição **não executada** (sem host de
medição).

## Objetivo

Fase Q2c de `docs/spec-query-v2.md` (§4.4): consultas com duas ou mais condições de igualdade,
a forma `namespace AND agent` dos clientes reais (registro 0033), não devem custar o tamanho
da sequência do primeiro termo.

## Decisões

- **Onde vale:** `QUERY2` sem `order_by`, com dois ou mais termos `EQ`. Com `order_by`, a
  varredura continua pela ordem pedida (§4.4). A v1 não muda, para continuar servindo de base.
- **Quem conduz:** o primeiro termo `EQ` na ordem do pedido. Alternativa: escolher o termo de
  sequência mais curta; descartada nesta fase porque exigiria contar as sequências (ou manter
  contadores por valor) e porque o leapfrog já limita o trabalho à sequência mais curta, com
  uma busca logarítmica por passo. Fica como otimização possível, a medir.
- **Busca em vez de avanço linear:** cada termo busca na sua árvore o primeiro inode maior ou
  igual ao alvo. Mais simples e com o mesmo limite de passos; um iterador com avanço local
  seria uma otimização de constante.
- **Cursor:** os resultados saem em ordem de inode dentro do valor do termo condutor, então a
  chave de retomada é (valor do condutor, último inode), a mesma forma de chave das fases
  anteriores; nenhuma mudança na UAPI.
- **Verificação do plano no teste:** sem um contador exposto na UAPI, a suíte verifica o plano
  pela ordem da resposta (opção `--raw` do cliente): com um primeiro termo `EXISTS` cujos
  valores decrescem com o inode, a varredura simples responderia em ordem decrescente de inode
  e a interseção responde em ordem crescente. Alternativa: um contador de candidatos em
  `GET_INFO`; descartada para não ampliar a UAPI só para teste.
- **Medição contra a Q2a/Q2b:** carregar no host os módulos das duas versões, alternadamente,
  e rodar o mesmo W3 por domínio (`bench-load --query2 1`) com a forma `namespace AND agent`.

## Mudanças

- `layer/index.c`: `add_candidate` (extraída do laço de varredura), `seek_eq`, `leap_collect`,
  campos `leap`, `nleap` e `leap_idx` no escopo da consulta, seleção do plano em `do_query2`.
- `tests/query.c`: `q2 --raw`.
- `vm/suite.sh`: bloco `QUERY2_LEAPFROG_PASS` (ver `docs/testing.md`).

## Evidências

`make vm-test`: `QUERY2_LEAPFROG_PASS`, `QUERY2_ORDER_PASS` e `QUERY2_PASS` em ext4, btrfs e
XFS, e todos os marcadores anteriores até `MOOBFS_LAYER_PASS`, na primeira execução. A
verificação do plano passou nos três inferiores (inodes crescentes com a criação dos arquivos).

## Limitações

- Sem medição de desempenho.
- O condutor é escolhido pela posição no pedido, não pelo tamanho da sequência.

## Próximo passo

PR da Q2c depois do merge da Q2b. Fase Q2d: lote e `MISSING_OK`.

## Nota (2026-10-12)

Medido no host do registro 0042, o avanço alternado descrito acima foi trocado por uma varredura
da sequência mais curta entre os termos `EQ`, escolhida por um percurso em paralelo; números e
justificativa no registro 0044. A verificação do plano pela ordem da resposta continua válida
(o plano revisado também responde em ordem crescente de inode).
