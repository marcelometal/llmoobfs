# 0050 — Pendências de código recebidas do lote de experiência do desenvolvedor

Data: 2026-10-12. Estado: implementadas nas branches da série Q2; checkpatch zerado e obrigatório.

## Contexto

Ao encerrar o plano de experiência do desenvolvedor (`docs/dx-plan.md`, registros 0039 a 0046),
a outra sessão passou quatro pendências que mexem em código ou no harness da Q2. Foram encaixadas
nas branches que já mexem nos mesmos arquivos, para não abrir um pull request concorrente com a
fila Q2 (cada atualização da main força uma nova execução do KASAN).

## Mudanças

1. **`qsort(NULL, 0, ...)` no oráculo** (`tools/moobfs-index.c`, branch da Q2d): só ordena quando
   há resultados. Reproduzido antes com `make bench-smoke` sob ASan e UBSan com
   `-fno-sanitize-recover=all`.
2. **Falso aprovado no `bench-smoke`** (achado ao reproduzir o item 1): `bench/equiv.sh`
   encadeava o oráculo em `cut` e `sort`, e o status verificado era o do `sort`; com o oráculo
   abortado pelo sanitizador, a resposta vazia coincidia com a esperada e o teste imprimia
   `BENCH_SMOKE_PASS`. O status do oráculo passou a ser verificado à parte. Comprovado: com o
   oráculo antigo o `bench-smoke` sob sanitizadores falha (código 2); com o corrigido, passa sem
   nenhum relatório.
3. **`bench-smoke` sob ASan e UBSan no CI** (job "Tools and sanitizers", branch da Q2d): compila
   com `-fsanitize=address,undefined -fno-sanitize-recover=all` e falha pelo status e por
   qualquer relatório de sanitizador na saída.
4. **`kasan.yml`**: as duas negações (`! grep ...`) que não eram o último comando de um passo
   `bash -e`, e portanto não o reprovavam (SC2251), viraram `if grep ...; then exit 1; fi`.
5. **checkpatch** (branch `m3/query-v2-recent`, onde `layer/index.c` está na forma final): eram
   30 avisos nessa branch (14 na main mais os do código da Q2). Corrigidos 28: linhas em branco
   em `kernel/main.c`; permissões em octal em `reach_ok`; comentários de bloco no estilo do
   kernel; a varredura da montagem e a cópia de um resultado extraídas em funções
   (`scan_child`, `scan_push_dir`, `scan_file`, `put_result`) para tirar a indentação profunda.
   Sem mudança de comportamento: `make vm-test` passou (`MOOBFS_VM_PASS`, blocos `QUERY2_*`,
   `DEMO_PASS`, `MOOBFS_LAYER_PASS`).

## Decisão: `EOPNOTSUPP` no lugar de `ENOSYS`

Com `noindex`, a v1 e a v2 devolviam `ENOSYS`; para o checkpatch, `ENOSYS` só significa "chamada
de sistema inexistente". O mantenedor escolheu trocar por `EOPNOTSUPP` (alternativa: manter e
excluir só essa regra no `scripts/checkpatch.sh`). Decisão de compatibilidade: a UAPI é
declarada experimental e nenhum teste dependia do código antigo, mas um cliente que testasse
`ENOSYS` precisa passar a testar `EOPNOTSUPP`. Com domínios desligados, a v2 já devolvia
`EOPNOTSUPP`; para distinguir os dois casos, o cliente lê `GET_INFO` (`MOOB_FLAG_INDEX_OFF`,
`MOOB_FLAG_DOMAINS`). A suíte passou a verificar o código: a v1 com `noindex` falha com
"Operation not supported" e a v2 devolve 95 (`EOPNOTSUPP`).

Com isso o checkpatch ficou com 0 erros e 0 avisos nessa branch; `scripts/checkpatch.sh` sai com
1 em qualquer aviso (conferido nas duas direções), e o passo deixou de ter `continue-on-error`
no job `Lint` (item S5 de `docs/security-roadmap.md`). As especificações antigas recebem nota
datada em vez de reescrita.
