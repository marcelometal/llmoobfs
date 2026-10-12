# 0035 — Fase Q2a: `QUERY` por domínio (UAPI v2), partições e regra de homogeneidade

Data: 2026-10-11. Estado: implementado e testado na VM QEMU da suíte (kernel Debian 6.12.111,
ext4, btrfs e XFS); medição de desempenho e teste do canal de tempo **não executados** (host
da Azure desligado; próximo host: Mac mini i5 local, a configurar pelo mantenedor).

## Objetivo

Primeira fase da especificação `docs/spec-query-v2.md` (ADR-0009, alternativa E): índice em
RAM particionado por domínio de permissão, regra de homogeneidade, `MOOB_IOC_QUERY2` sem
`order_by` e a restrição da v1.

## Mudanças

- **Montagem:** opção `domain_depth=N` (0 a 8, padrão 1; 0 desliga os domínios).
  `show_options` só a mostra quando difere de 1.
- **UAPI (`include/moobfs_ioctl.h`):** `MOOB_IOC_QUERY2` (`_IOWR(.., 3, struct moob_query2)`),
  versão 2, com `order_by`, `order`, `cursor` e `reserved` já no layout e obrigatoriamente zero
  nesta fase. `struct moob_info` usa parte do espaço reservado para `domain_depth`,
  `heterogeneous` e `fallback_domains`, sem mudar o tamanho; nova marca `MOOB_FLAG_DOMAINS`.
- **Índice (`layer/index.c`):** a árvore de nomes passa a ser ordenada por (nome, domínio,
  heterogêneo); cada registro de inode guarda seu domínio e se é heterogêneo, e as entradas
  mudam de partição quando isso muda. A varredura da montagem carrega profundidade, domínio e o
  resultado da regra pelos diretórios intermediários.
- **Regra de homogeneidade:** como na especificação, com três complementos registrados nela
  (seção "Implementação Q2a"): diretórios intermediários avaliados pelo bit de execução; arquivo
  com `nlink > 1` heterogêneo; ACL estendida detectada por `get_inode_acl` com mais de três
  entradas.
- **Reavaliação:** `setattr` (modo, dono, grupo), `set_acl` de acesso, `link` e `rename`
  recalculam a partição do inode. Mudança de permissão de um diretório na profundidade do
  domínio ou abaixo dela coloca o domínio em **fallback** (verificação por resultado, como na
  v1) até a próxima montagem.
- **`QUERY2`:** sobre o descritor do diretório do domínio; verifica `MAY_EXEC | MAY_READ` uma
  vez e devolve a partição homogênea sem verificar cada resultado (salvo em fallback); caminhos
  relativos ao domínio. Erros: `ENOSYS` sem índice, `EOPNOTSUPP` com `domain_depth=0`, `ENOTTY`
  fora de um diretório de domínio, `EACCES` sem permissão, `EINVAL` para campos inválidos ou
  ainda não suportados.
- **v1:** com domínios ligados, `EPERM` sem `CAP_SYS_ADMIN`; com `domain_depth=0`, inalterada.
- **Renomeação:** diretório que mudaria de domínio ou de profundidade recebe `EXDEV` antes de
  qualquer trava; arquivo muda de partição na mesma chamada.
- **Ferramentas:** `moobfs-index --domain` (regra reimplementada de forma independente, a partir
  de `stat` e `getxattr`); `tests/query.c` com `q2`, `q2errno`, `q2invalid` e `rename`;
  `bench-load --query2 1` (W3 por domínio) e `--mount-opts` (W6/W7); `bench/run.sh` com
  `LAYER_MOUNT_OPTS` (padrão `domain_depth=0`, para manter comparáveis as campanhas da v1);
  `bench/timing-v2.sh` para o teste 4 da especificação.
- **Suíte:** as montagens existentes passam a usar `domain_depth=0` (os testes de isolamento da
  v1 rodam como usuário comum); novo bloco `QUERY2_PASS` por inferior (ver `docs/testing.md`).

## Erros encontrados e corrigidos

1. Primeira rodada da suíte: `oops` em `depth_and_domain` no teste de `EXDEV` por mudança de
   profundidade. O destino do `rename` (dentry negativo) estava exatamente na profundidade do
   domínio, e o código lia o inode dele. Correção: dentry negativo não tem domínio.
2. Revisão do caminho de `rename`: `moobl_index_renamed` era chamado com o dentry de origem,
   mas o VFS só move o dentry depois que `->rename` retorna; a partição seria calculada no local
   antigo. Correção: o inode movido é posicionado pelo dentry de destino (também para
   `RENAME_EXCHANGE`); o caminho registrado no índice passa a ser o novo, o que também corrige
   o caminho guardado pela v1 após renomear um arquivo.
3. O roteiro do teste do canal de tempo criaria os diretórios de domínio com o layer montado,
   e o `chown`/`chmod` deles poria os domínios em fallback; o roteiro remonta antes de medir.

## Evidências

`make vm-test` (VM QEMU descartável, Docker): `QUERY2_PASS` em ext4, btrfs e XFS, seguido de
todos os marcadores anteriores (`ISOLATION_PASS`, `EQUIV_PASS`, `BENCH_LAYER_PASS`,
`LAYER_LOWER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`). KASAN: a ser executado pelo
CI no pull request.

Testes da especificação cobertos nesta fase: 1 (campos da v2 que existem nesta fase), 2 (forma
`kind eq` contra o oráculo por domínio, nos três inferiores), 3, 5, 6 (já antecipado da Q2e) e
9 em parte (v1 restrita; KASAN pendente do CI). **Não executado:** 4 (canal de tempo), que
depende de um host de medição.

## Limitações

- Domínio em fallback só volta ao modo rápido na próxima montagem.
- Arquivo que volta a ter um só nome continua heterogêneo até a próxima mudança de atributo de
  permissão, renomeação ou montagem (falha segura).
- A equivalência v2 contra o oráculo cobre um predicado; as nove formas do W3 por domínio ficam
  para a medição no host (`bench-load --query2 1`).
- O teste de concorrência (8) não foi estendido aos domínios.
- ACLs estendidas tiram o arquivo da partição (decisão aceita).

## Próximo passo

Pull request com KASAN. No host de medição: `bench/timing-v2.sh` (teste 4) e W3 por domínio
comparado ao registro 0029. Depois, fase Q2b (`order_by` e `cursor`).

## Atualização (2026-10-11)

KASAN e lockdep no CI do pull request 31 (kernel 6.12.112-virtme): `QUERY2_PASS` em ext4,
btrfs e XFS e `MOOBFS_LAYER_PASS`, sem relatório do KASAN; execução 38170220257. Merge na main
no mesmo dia.
