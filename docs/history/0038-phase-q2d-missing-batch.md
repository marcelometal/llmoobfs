# 0038 — Fase Q2d: termos "ausente ou ..." e lote no `QUERY2`

Data: 2026-10-11. Estado: implementado e testado na VM QEMU da suíte (kernel Debian 6.12.111,
ext4, btrfs e XFS); KASAN pelo CI no pull request; medição **não executada** (sem host de
medição).

## Objetivo

Fase Q2d de `docs/spec-query-v2.md`: a marca `MOOB_TERM_MISSING_OK` (validade e expiração do
tipo "sem prazo ou prazo no futuro", registro 0033) e `MOOB_IOC_QUERY2_BATCH` (o "ou" entre
valores, como várias etiquetas ou tipos, numa chamada).

## Decisões

- **Lote com pedidos completos** em vez de resultados concatenados num vetor único (desvio
  registrado na especificação): cada pedido mantém o próprio `limit` e o próprio cursor. A
  alternativa concatenada exigiria repartir um limite único entre pedidos e um cursor
  composto.
- **Validação total antes de executar:** um pedido inválido recusa o lote inteiro, e nada é
  escrito de volta. Alternativa: executar até o primeiro erro; descartada porque deixaria
  respostas parciais difíceis de interpretar.
- **Sem fotografia única:** cada pedido adquire o lock do índice separadamente, como uma
  sequência de chamadas `QUERY2` com uma só verificação de permissão. Segurar o lock pelo lote
  inteiro bloquearia escritas por mais tempo sem um ganho que os clientes peçam.
- **Byte de marcas:** `reserved[0]` do termo passa a se chamar `flags` (mesmo lugar e
  tamanho). A v1 recusa qualquer marca.
- **"Ausente" segue o índice:** tipo diferente é presente (não casa); valor inválido ou bruto
  não é indexado e conta como ausente. O oráculo foi ajustado para a mesma regra.
- **Condutor da varredura:** o primeiro termo sem a marca, numa cópia dos termos usada só para
  executar; o resumo do cursor continua calculado sobre o pedido como veio. `EXISTS` com a
  marca é recusado. Sem `order_by`, pelo menos um termo sem a marca (§4.1).
- **Sintaxe nas ferramentas:** operador terminado em `?` (`gt?`, `eq?`). A suíte desliga a
  expansão de nomes do shell (`set -f`) no bloco, porque `gt?` é um padrão de glob.

## Mudanças

- `include/moobfs_ioctl.h`: `flags` no termo, `MOOB_TERM_MISSING_OK`, `struct moob_query2_batch`,
  `MOOB_QUERY2_BATCH_MAX`, `MOOB_IOC_QUERY2_BATCH`.
- `layer/index.c`: `term_check` com marcas permitidas por versão; casamento "ausente ou ...";
  `do_query2` dividido em `query2_target`, `query2_validate` e `query2_exec`;
  `do_query2_batch`.
- `tools/query-common.h`: operador com `?`. `tools/moobfs-index.c`: regra de ausente.
- `tests/query.c`: `q2batch`, `q2batcherrno`, casos inválidos de marcas e de lote, e a v1
  recusando marcas.
- `vm/suite.sh`: bloco `QUERY2_MISSING_BATCH_PASS` (ver `docs/testing.md`).

## Evidências

`make vm-test`: `QUERY2_MISSING_BATCH_PASS`, `QUERY2_LEAPFROG_PASS`, `QUERY2_ORDER_PASS` e
`QUERY2_PASS` em ext4, btrfs e XFS, e todos os marcadores anteriores até `MOOBFS_LAYER_PASS`,
na primeira execução. Shellcheck 0.10.0 (`--severity=warning`) sem achados nos scripts.

## Limitações

- Sem medição; o lote economiza chamadas e verificações de permissão, mas cada pedido ainda
  percorre a sua parte do índice.
- Lote sob mudanças concorrentes não foi testado.

## Próximo passo

PR da Q2d depois dos merges da Q2b e da Q2c. Fase Q2e (renomeações entre domínios e teste de
concorrência com domínios); a parte de `EXDEV` e da mudança de partição de arquivos já entrou
na Q2a.
