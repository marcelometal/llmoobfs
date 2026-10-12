# 0045 — Triagem dos 30 alertas abertos do CodeQL

Data: 2026-10-12. Branch: `fix/codeql-triage`, criada a partir de `origin/main` (`fccf38a`,
merge do PR #41). PR: https://github.com/studiomoob/llmoobfs/pull/42. Estado: correções de
código implementadas e testadas; as dispensas no GitHub ficam com o mantenedor (escritas na API
de code scanning negadas nesta sessão; ver "Comandos para o mantenedor").

## Objetivo

Dar uma decisão a cada um dos 30 alertas abertos do CodeQL (`security-extended`, C de espaço de
usuário em `tools/`, `common/` e `bench/`, introduzido no registro 0039), antes de responder ao
critério `static_analysis_fixed` do OpenSSF Best Practices (projeto 15408). Nada em `layer/`,
`kernel/`, no formato, na UAPI ou nos formatos de manifesto e de registro do benchmark mudou.

Numeração: conferidas `origin/main` (último 0041), todas as branches remotas (`m3/query-v2-q2b`
a `q2d` usam 0036 a 0038; `m3/query-v2-q2d` também 0042 a 0044) e as branches locais. O 0040
continua reservado para a fase Q2e. A branch local `codex/kernel-index-reuse-research`, não
publicada, ainda tem um `0039-kernel-index-reuse-research.md` duplicado e precisa ser renumerada
para 0046 ou depois.

## Critério

- (a) Corrigir quando há problema real ou quando uma mudança pequena e clara elimina o padrão sem
  mudar comportamento.
- (b) Dispensar quando o alerta descreve o uso pretendido: ferramentas de linha de comando que
  abrem o caminho que o próprio usuário passou e harness de benchmark que lê um manifesto e
  arquivos de consulta escolhidos pelo operador. Nenhum desses binários é instalado com setuid;
  o `load --uid N` só reduz privilégio (`setgroups`/`setgid`/`setuid` para um usuário comum),
  nunca o eleva. Não há fronteira de privilégio entre quem fornece o dado e quem executa.

Motivos de dispensa: `false positive` para `tools/` (o fluxo existe, mas não é uma
vulnerabilidade: abrir o caminho pedido é a função da ferramenta); `used in tests` para
`bench/` (harness de medição, não instalado nem usado em produção). `won't fix` não foi usado,
porque sugeriria um defeito real aceito.

## Decisões por alerta

| Alerta | Regra | Local (em `main`) | Decisão | Justificativa |
| --- | --- | --- | --- | --- |
| 27 | `cpp/unbounded-write` | `bench/common.h:93` | Corrigido | Falso positivo confirmado: `strlen(tok) >= BENCH_PATH_MAX` já rejeitava antes do `strcpy` para `path[BENCH_PATH_MAX]`; agora `memcpy` do tamanho verificado contra `sizeof out->path` |
| 28 | `cpp/unbounded-write` | `bench/common.h:113` | Corrigido | Falso positivo confirmado: `strlen(tok) > 64` para `name[65]`; agora `n >= sizeof a->name` e `memcpy`, mesma fronteira |
| 29 | `cpp/unbounded-write` | `bench/common.h:117` | Corrigido | Falso positivo confirmado: `strlen(colon) >= sizeof a->value` antes do `strcpy`; agora `memcpy` do tamanho verificado |
| 30 | `cpp/world-writable-file-creation` | `bench/sqlclient.c:53` | Corrigido | Arquivo de registros criado por `bench_create_out()` (modo 0644 antes da umask) |
| 31 | `cpp/world-writable-file-creation` | `bench/load.c:536` | Corrigido | Idem (registros do W3 com `--uid`) |
| 32 | `cpp/world-writable-file-creation` | `bench/load.c:551` | Corrigido | Idem (registros do leitor do W5) |
| 33 | `cpp/world-writable-file-creation` | `bench/load.c:445` | Corrigido | Idem (registros por processo) |
| 34 | `cpp/world-writable-file-creation` | `bench/load.c:412` | Corrigido | `/proc/sys/vm/drop_caches` é um arquivo existente; aberto com `open(O_WRONLY)` sem `O_CREAT`, então nada pode ser criado |
| 35 | `cpp/world-writable-file-creation` | `bench/gen.c:81` | Corrigido | Manifesto criado por `bench_create_out()` |
| 6 | `cpp/sql-injection` | `bench/sqlclient.c:72` | Dispensar (`used in tests`) | `sqlclient` é a linha de base SQLite do W3: executa, por definição, as consultas do arquivo que o operador passa (`bench/queries-w3.txt`); não é serviço nem recebe entrada de terceiros |
| 7 | `cpp/path-injection` | `bench/sqlclient.c:53` | Substituído pelo 38 (dispensar o 38); ver nota | Prefixo de saída `--out` do operador |
| 8 | `cpp/path-injection` | `bench/sqlclient.c:56` | Dispensar (`used in tests`) | Arquivo SQL `--sql` do operador |
| 9 | `cpp/path-injection` | `tools/fsck.c:43` | Dispensar (`false positive`) | `fsck.moobfs IMAGE` lê a imagem que o próprio usuário indicou, só leitura, com os privilégios dele |
| 10 | `cpp/path-injection` | `bench/load.c:536` | Substituído pelo 39 (dispensar o 39); ver nota | Prefixo `--out` do operador |
| 11 | `cpp/path-injection` | `bench/load.c:551` | Substituído pelo 40 (dispensar o 40); ver nota | Prefixo `--out` do operador |
| 12 | `cpp/path-injection` | `bench/load.c:445` | Substituído pelo 41 (dispensar o 41); ver nota | Prefixo `--out` do operador |
| 13 | `cpp/path-injection` | `bench/load.c:454` | Dispensar (`used in tests`) | `--root` do operador (W8) |
| 14 | `cpp/path-injection` | `bench/load.c:460` | Dispensar (`used in tests`) | `--manifest` do operador |
| 15 | `cpp/path-injection` | `bench/load.c:418` | Dispensar (`used in tests`) | `--target` do operador (W6/W7) |
| 16 | `cpp/path-injection` | `bench/load.c:366` | Dispensar (`used in tests`) | `--queries` do operador (W8) |
| 17 | `cpp/path-injection` | `bench/load.c:328` | Dispensar (`used in tests`) | `--root`/`--target` do operador (consulta) |
| 18 | `cpp/path-injection` | `bench/load.c:291` | Dispensar (`used in tests`) | `--root` do operador (W3) |
| 19 | `cpp/path-injection` | `bench/load.c:274` | Dispensar (`used in tests`) | `--target` + caminho do manifesto gerado por `bench-gen` (W5) |
| 20 | `cpp/path-injection` | `bench/load.c:218` | Dispensar (`used in tests`) | `--queries` do operador (W3) |
| 21 | `cpp/path-injection` | `bench/load.c:171` | Dispensar (`used in tests`) | `--target` + caminho do manifesto (W2) |
| 22 | `cpp/path-injection` | `bench/load.c:143` | Dispensar (`used in tests`) | `--target` + caminho do manifesto (W1, arquivo temporário, já 0644) |
| 23 | `cpp/path-injection` | `bench/load.c:152` | Dispensar (`used in tests`) | Diretório pai do mesmo caminho (W1, `fsync`) |
| 24 | `cpp/path-injection` | `tools/mkfs.c:52` | Dispensar (`false positive`) | `mkfs.moobfs` lê os arquivos-fonte `NAME=SOURCE` que o usuário passou, só leitura |
| 25 | `cpp/path-injection` | `bench/gen.c:81` | Substituído pelo 42 (dispensar o 42); ver nota | `--out` do operador |
| 26 | `cpp/path-injection` | `tools/mkfs.c:88` | Dispensar (`false positive`) | `mkfs.moobfs NEW_IMAGE`: cria a imagem que o usuário pediu, com `O_EXCL` (não sobrescreve) e modo 0600 |

Resumo: 9 corrigidos (3 `unbounded-write`, 6 `world-writable-file-creation`); 21 com decisão
de dispensa (20 `path-injection`, 1 `sql-injection`), dos quais 16 dispensados pelo número
atual e 5 (7, 10, 11, 12, 25) pelo alerta novo que os substitui no PR (38 a 42). Nenhum alerta
revelou um defeito explorável.

Nota sobre os alertas 7, 10, 11, 12 e 25: a criação dos arquivos de saída passou para
`bench_create_out()` em `bench/common.h`, então o `fopen` desses cinco fluxos deixou de existir.
O CodeQL reporta os mesmos fluxos como alertas novos (38 a 42); ver "Resultado do CodeQL no PR".

## Mudanças

- `bench/common.h`: `bench_parse_line()` copia `path`, `name` e `value` com `memcpy` do
  comprimento já verificado contra `sizeof` do destino (antes, `strcpy` depois da mesma
  verificação; para `name`, `> 64` virou `>= sizeof a->name`, idêntico com `name[65]`). Novo
  `bench_create_out()`: `open(O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)` + `fdopen`.
  O comentário de topo deixa de dizer "no I/O" e cita o helper.
- `bench/load.c`, `bench/gen.c`, `bench/sqlclient.c`: os cinco `fopen(..., "w"/"wb")` de saída
  usam `bench_create_out()`. No W6, `drop_caches` é aberto com `open(O_WRONLY | O_CLOEXEC)` e
  `fdopen`, sem `O_CREAT`.

Efeito observável: só o modo dos arquivos de saída quando a umask não remove a escrita de
"outros". Com a umask usual 022 o resultado continua 0644. Formatos de manifesto e de registro
inalterados (o `bench-smoke` confere determinismo e compara com o oráculo).

## Testes executados

Em contêiner `ubuntu:24.04` (aarch64, Docker no macOS), a partir de uma cópia limpa da branch:

| Comando | Resultado |
| --- | --- |
| `make test` | 9 testes, OK |
| `make test CC=clang BUILD=build/asan CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'` (o mesmo passo do CI) | 9 testes, OK |
| `make bench-smoke` | `BENCH_SMOKE_PASS` |
| `make bench-smoke CC=clang BUILD=build/asan-bench CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all' LDFLAGS='-fsanitize=address,undefined'` | `BENCH_SMOKE_PASS`; um relatório do UBSan em `tools/moobfs-index.c:194`, preexistente (ver limitações) |
| `umask 000; bench-gen --seed 1 --inodes 5 --profile 6 --out /tmp/m.txt; stat -c %a` | 644 nesta branch; 666 em `origin/main` (mesmo contêiner) |

A mesma execução do `bench-smoke` em `origin/main` imprimiu as mesmas linhas de saída (inclusive
`load: proc 0: 300 failed operations (recorded as errors)`, esperado no diretório comum).
Não executado localmente: `make vm-test` (a mudança não toca o que a VM exercita além do
`bench-smoke`; o job `Native Linux VM` do CI roda no PR).

CI do PR #42 em `da1a760` (runs 38244739816 e 38244739750): `Lint`, `Tools and sanitizers`
(inclui `make test` com ASan/UBSan e `make bench-smoke`) e `Analyze C (tools, common, bench)`
passaram; `CodeQL` falhou pelos alertas novos 38 a 42 (seção seguinte); `Native Linux VM`
pendente no momento da escrita.

## Resultado do CodeQL no PR

A análise do PR (run do workflow `CodeQL` em `da1a760`) não reportou nenhum dos alertas 27 a 35
nas linhas alteradas, mas abriu cinco alertas novos de `cpp/path-injection`, 38 a 42, e por isso
o check `CodeQL` do PR ficou vermelho ("5 new alerts including 5 high severity security
vulnerabilities"):

| Novo | Local (na branch) | Substitui |
| --- | --- | --- |
| 38 | `bench/sqlclient.c:53` | 7 |
| 39 | `bench/load.c:540` | 10 |
| 40 | `bench/load.c:555` | 11 |
| 41 | `bench/load.c:449` | 12 |
| 42 | `bench/gen.c:81` | 25 |

São os mesmos fluxos (prefixo `--out` do operador), agora com a mensagem "passed to
`bench_create_out(path)`, which calls `open`": o texto da linha mudou, então a impressão digital
do alerta também mudou. Qualquer correção do modo nesses cinco pontos tem esse efeito. O CodeQL
só considera `umask()` chamada na mesma função, e uma `umask` global mudaria também o modo dos
diretórios criados pelo W1. Alternativa descartada: deixar os cinco `fopen` como estavam e
dispensar 30, 31, 32, 33 e 35, o que manteria o check verde e os arquivos em 0666 menos a umask.
Ficou a correção, porque o pedido era o modo explícito. Decisão para 38 a 42: dispensar como
`used in tests`, com a mesma justificativa dos alertas que substituem. Os antigos 7, 10, 11, 12
e 25 devem fechar como `fixed` na primeira análise de `main` depois do merge.

A análise de PR é restrita às linhas alteradas (diff-informed), então a ausência dos alertas
27 a 35 na branch só fica confirmada pela análise completa de `main` depois do merge.

## Comandos para o mantenedor

Dispensar os 21 alertas da tabela (exceto 7, 10, 11, 12 e 25, que serão substituídos) e os cinco
novos. Rodar 38 a 42 antes do merge, para que o PR fique sem alertas novos abertos (a página do
PR e o check passam a refletir a dispensa; não confirmado nesta sessão). Os demais podem ser
rodados a qualquer momento.

```sh
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/9 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: fsck.moobfs reads the image path its own user passed, read-only. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/24 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: mkfs.moobfs reads the NAME=SOURCE files its own user passed, read-only. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/26 -f state=dismissed -f dismissed_reason='false positive' -f dismissed_comment='Intended CLI use: mkfs.moobfs creates the image its own user named, O_EXCL and mode 0600. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/8 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --sql file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/13 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/14 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --manifest. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/15 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/16 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --queries file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/17 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root/--target. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/18 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --root. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/19 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/20 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --queries file. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/21 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/22 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --target plus a manifest path from bench-gen. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/23 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator parent of a --target manifest path. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/38 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 7). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/39 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 10). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/40 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 11). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/41 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out prefix (replaces alert 12). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/42 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='Benchmark harness: path from the operator --out manifest path (replaces alert 25). Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
gh api -X PATCH repos/studiomoob/llmoobfs/code-scanning/alerts/6 -f state=dismissed -f dismissed_reason='used in tests' -f dismissed_comment='sqlclient is the SQLite baseline of the benchmark and runs, by design, the SQL file its operator passes; not a service. Not setuid; no privilege boundary. Triage: docs/history/0045-codeql-triage.md'
```

Conferência depois do merge e da análise de `main`:

```sh
gh api 'repos/studiomoob/llmoobfs/code-scanning/alerts?state=open&tool_name=CodeQL&per_page=100' --jq 'length'
```

Esperado: 0. Os alertas 7, 10 a 12, 25 e 27 a 35 devem aparecer com `state=fixed`.

## Limitações

- O UBSan aponta `qsort(NULL, 0, ...)` em `tools/moobfs-index.c:194` quando nenhum arquivo
  casa; reproduzido também em `origin/main`, fora do escopo desta triagem e não coberto pelo
  passo de sanitizers do CI (que roda só `make test`). Correção trivial (`if (nhits)`), deixada
  como próximo passo.
- As dispensas valem enquanto os binários não forem instalados com setuid nem expostos a
  entrada de terceiros; uma mudança nisso reabre a análise.
- O `make bench-smoke` sob sanitizers foi executado aqui, mas não está no CI.

## Próximo passo

Mantenedor: dispensar 38 a 42 (o check `CodeQL` do PR depende disso), fazer o merge, dispensar
os demais, conferir que não resta alerta aberto e responder `static_analysis_fixed` citando este
registro. Depois, corrigir o `qsort` de `moobfs-index` e considerar o
`bench-smoke` com sanitizers no CI.
