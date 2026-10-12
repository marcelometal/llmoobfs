# 0036 — Fase Q2b: ordenação e cursor no `QUERY2`

Data: 2026-10-11. Estado: implementado e testado na VM QEMU da suíte (kernel Debian 6.12.111,
ext4, btrfs e XFS); KASAN pelo CI no pull request; medição **não executada** (sem host de
medição: a VM da Azure está desligada e o Mac mini local ainda será configurado).

## Objetivo

Fase Q2b de `docs/spec-query-v2.md`: `order_by` com `ASC`/`DESC` e `cursor` de paginação,
para a consulta mais comum dos clientes reais ("os N mais recentes", registro 0033).

## Decisões

- **Tipo de ordenação explícito** (`order_type`, tirado do preenchimento; a estrutura não muda
  de tamanho). Alternativa: ordenar todas as entradas do nome, de todos os tipos, na ordem do
  índice (tipo primeiro); descartada porque misturaria tempos com inteiros e não daria como
  recusar "tipo errado", que a especificação pede.
- **Atributo de ordenação ausente no domínio** devolve resposta vazia, não `EINVAL`: a ausência
  não é erro de entrada. O teste 1 da especificação foi lido assim; registrado nela.
- **Cursor por chave** (tipo, valor, inode do último resultado entregue), autenticado com
  SipHash e uma chave aleatória por montagem, junto com um resumo da consulta que inclui o
  estado de fallback do domínio. Alternativa: guardar no cursor um ponteiro ou um contador de
  posição; descartada porque não sobrevive a mudanças na árvore e permitiria cursores forjados
  apontando para estado interno. O cursor não é segredo: forjar um só deixa retomar de outra
  posição do mesmo domínio, ao qual o chamador já tem acesso; a autenticação serve para
  detectar cursor de outra consulta ou montagem e corrompido.
- **Valores longos** (mais de 96 bytes) são recuperados da entrada atual do inode; se ela mudou
  de tipo, tamanho ou prefixo, `ESTALE`.
- **Sem `order_by`, `nterms` continua ≥ 1; com `order_by`, pode ser 0** ("os N mais recentes"
  do domínio inteiro).
- O motor (`query_run`) foi generalizado: percorre a árvore do atributo de varredura nos dois
  sentidos, retoma estritamente depois (ou antes) de uma chave e devolve a chave do último
  resultado. A v1 usa o mesmo caminho, sem cursor, com comportamento inalterado.

## Mudanças

- `include/moobfs_ioctl.h`: `MOOB_ORDER_ASC`, `MOOB_ORDER_DESC`, `order_type`, contrato do
  cursor no comentário.
- `layer/index.c`: `inode_matches_all`, `last_before`, `seek_type`, cursor (`query_hash`,
  `cursor_encode`, `cursor_decode`), `order_check`, `do_query2` com ordenação e cursor.
- `layer/moobl.h`: chave do cursor por montagem.
- `tests/query.c`: `q2` com `--order`, `--limit`, `--pages`, `--cursor-in`, `--cursor-out`;
  `q2invalid` com os campos de ordenação, cursor lixo (`ESTALE`) e atributo ausente.
- `tools/moobfs-index.c`: `--order NAME TYPE asc|desc` e `--limit N`.
- `bench/load.c`: W3 v2 com `--order NAME:TYPE:asc|desc` e `--limit N`.
- `vm/suite.sh`: bloco `QUERY2_ORDER_PASS` (ver `docs/testing.md`).

## Evidências

`make vm-test`: `QUERY2_ORDER_PASS` e `QUERY2_PASS` em ext4, btrfs e XFS, e todos os
marcadores anteriores até `MOOBFS_LAYER_PASS`. A suíte passou na primeira execução. O
`bench-load` com `--order` só foi compilado; não foi executado.

## Limitações

- Paginação sob mudanças concorrentes não foi testada; a semântica documentada é a de
  paginação por chave.
- Cursor de valor longo depende do valor atual do inode.
- Sem medição: o custo de "N mais recentes" com termos seletivos é proporcional às entradas
  percorridas na ordem até completar o limite, e isso ainda não foi medido.

## Próximo passo

PR da Q2b depois do merge da Q2a. No host de medição: o teste do canal de tempo, o W3 por
domínio e "N mais recentes". Depois, fase Q2c (interseção por leapfrog).

## Atualização (2026-10-11)

O job `Lint` do pull request 32 falhou: o shellcheck (SC2209) leu a atribuição literal
`t=time` da suíte como atribuição da saída de um comando. Corrigido com aspas no commit
`vm: quote a literal assignment...`; o shellcheck 0.10.0 com `--severity=warning` passou
localmente nos scripts de `vm/` e `scripts/` antes do novo push. O `make lint` local não roda
dentro do contêiner a partir de uma worktree, porque `git ls-files` não encontra o repositório;
os arquivos foram passados diretamente.

KASAN e lockdep no CI do pull request 32 (kernel 6.12.112-virtme), execução 38173795652:
`QUERY2_ORDER_PASS` e `QUERY2_PASS` em ext4, btrfs e XFS e `MOOBFS_LAYER_PASS`, sem relatório.
A etapa da suíte levou 50 minutos (22 na Q2a), pelo custo das consultas paginadas no kernel
instrumentado. O merge foi recusado por conflito com a main (lote 2 de ferramentas da outra
sessão, registro 0039, só no changelog e no índice de registros); resolvido mantendo as duas
entradas, o que exige nova execução do CI antes do merge.

O CodeQL, que entrou na main com o lote 2 da outra sessão, apontou duas escritas "sem limite"
(`cpp/unbounded-write`) em `strcpy` de argumentos de linha de comando, em `tools/moobfs-index.c`
e `bench/load.c`. Os dois casos já eram precedidos de verificação de tamanho (falsos
positivos), mas foram trocados por `memcpy` com o tamanho verificado contra o destino, o que
torna o limite explícito; o mesmo padrão em `tests/query.c`, que o CodeQL não compila, também
foi trocado.

Atualização posterior (2026-10-12): a exceção acima não chegou a ser usada. Antes do merge, a main
recebeu também o PR 42 (triagem do CodeQL, com mudanças em `bench/`) e os PRs 43 a 48 do lado DX;
como a mudança deixou de ser só documentação, a branch foi atualizada com a main final, o
changelog resolvido mantendo as duas entradas, e o merge espera uma nova execução do KASAN.
