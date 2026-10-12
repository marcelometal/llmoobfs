# 0039 — Experiência do desenvolvedor no GitHub: lote 2 (ferramentas)

Data: 2026-10-11. Branch: `community/dx-batch2`, criada a partir de `origin/main` (`0a7bf54`,
merge do PR #27) e rebaseada, antes de publicada, sobre `8a4f1db` (merge do PR #24). Estado: implementado; a medição em Codespaces, a inscrição no OpenSSF Best Practices
e duas configurações do repositório ficam pendentes (ver Limitações).

## Objetivo

Executar o lote 2 de [`docs/dx-plan.md`](../dx-plan.md): devcontainer, OpenSSF Scorecard,
CodeQL para o C de espaço de usuário, `checkpatch.pl` como aviso e labels por caminho com
mensagem de primeira contribuição. Nada em `layer/`, `kernel/` ou no formato mudou.

Numeração: conferidas `origin/main` e todas as branches remotas; a maior era 0034 (registros
0033 e 0034 do PR #24, que entrou em `main` durante esta rodada). Este registro ficou com 0035 (ver a nota de renumeração no fim).

## Decisões

| Tema | Decisão | Justificativa resumida |
| --- | --- | --- |
| Imagem do devcontainer | `mcr.microsoft.com/devcontainers/base:3.0.10-ubuntu24.04`, fixada por digest | Mesma versão do Ubuntu dos runners do CI: `shellcheck` 0.9.0, gcc 13 e clang 18 iguais aos do CI |
| Docker no devcontainer | Feature `docker-in-docker:4` (Moby, sem Compose) | `vm/run.sh` precisa só de `docker build` e `docker run` sem privilégio; o guest QEMU continua sendo o único lugar onde o módulo é carregado |
| Ferramentas do devcontainer | `.devcontainer/post-create.sh` instala pelo apt, sem features de terceiros | Lista curta e legível; `libclang-rt-18-dev` entrou depois que `make fuzz` falhou sem ele |
| `hostRequirements` e botão "Open in Codespaces" | Não declarar nem anunciar ainda | O plano exige medir tempo, disco e máquina mínima num Codespace real antes |
| Scorecard | `push` em `main` e agenda semanal, `publish_results: true`, SARIF no code scanning | Só esses gatilhos são suportados; a publicação exige o badge e restringe o workflow às actions aprovadas |
| Permissões | `contents: read` no topo de cada workflow novo; escrita só no job que precisa | Menor privilégio; o Scorecard proíbe escrita no nível do workflow |
| CodeQL | Workflow avançado, `build-mode: manual`, `make tools attr-tools bench-tools`, consulta `security-extended` | Para C, só é extraído o que o compilador vê; o build manual delimita a análise a `tools/`, `common/`, `bench/` e aos headers e testes que eles compilam. O "default setup" está desligado no repositório, então não há conflito |
| `checkpatch.pl` | `scripts/checkpatch.sh` + `make checkpatch`, passo com `continue-on-error` no job `Lint` | O comando local e o CI ficam iguais; aviso porque a linha de base ainda tem 14 avisos |
| Origem do `checkpatch.pl` | Tag estável `v6.12.112` (a mesma do kernel KASAN), de git.kernel.org com o espelho `gregkh/linux` no GitHub como alternativa, SHA-256 verificado | Os dois servidores entregam os mesmos bytes; o hash torna a origem indiferente para a integridade |
| Labeler e saudação | `pull_request_target`, sem checkout do código do PR | Necessário para rotular e comentar em PRs vindos de forks; seguro porque nada do PR é executado |
| Labels removidos | `sync-labels: false` | O bot só acrescenta; a escolha manual do mantenedor não é desfeita |
| Dependabot | Ecossistema `devcontainers` além de `github-actions` | Atualiza a feature do devcontainer como já se faz com as actions |
| CodeQL para workflows (`actions`) | Não incluído | Fora do pedido; candidato a um próximo passo barato (`build-mode: none`) |

Versões fixadas por SHA do commit (tags anotadas resolvidas para o commit), conferidas com
`git ls-remote` em 2026-10-11:

| Action | Versão | Commit |
| --- | --- | --- |
| `ossf/scorecard-action` | v2.4.4 | `2d1146689b8cda280b9bc96326124645441f03bc` |
| `github/codeql-action` (`init`, `analyze`, `upload-sarif`) | v4.38.3 | `24c54180a607b1449ed407dd24f251e4e9147c8d` |
| `actions/labeler` | v7.0.0 | `bf12e9b00b37c5c0ca2b87b79b2daf7891dbda13` |
| `actions/first-interaction` | v3.1.0 | `1c4688942c71f71d4f5502a26ea67c331730fa4d` |
| `actions/checkout`, `actions/upload-artifact` | v7.0.1, v7.0.2 | iguais aos de `ci.yml` |

Documentação consultada com `npx ctx7@latest` (`/ossf/scorecard`, `/websites/codeql_github`,
`/devcontainers/features`, `/websites/containers_dev`) e, para os detalhes que o Context7 não
trouxe, os `README.md` e `action.yml` das próprias actions na tag fixada. Um achado: o README
do `actions/first-interaction` v3.1.0 mostra `issue-message`/`pr-message`, mas o `action.yml`
declara `issue_message`/`pr_message`; o workflow usa os nomes do `action.yml`.

## Mudanças

- Novos: `.devcontainer/devcontainer.json`, `.devcontainer/post-create.sh`,
  `.github/workflows/{scorecard,codeql,labeler,first-interaction}.yml`, `.github/labeler.yml`,
  `scripts/checkpatch.sh` e este registro. Todos com linha SPDX.
- `Makefile`: alvo `checkpatch` e linha no `make help`.
- `.github/workflows/ci.yml`: passo `make checkpatch` no job `Lint`, com `continue-on-error`.
- `.github/dependabot.yml`: ecossistema `devcontainers`.
- `README.md`: badge do OpenSSF Scorecard ao lado do badge de CI. O `README.pt-BR.md` não tem
  badges e não foi alterado.
- `docs/development.md`: seção do devcontainer (sem anunciar Codespaces), `make checkpatch` e
  correção de uma frase antiga que dizia que o CI roda `shellcheck` só em `vm/*.sh`.
- `docs/labels.md`: regra do labeler. `docs/dx-plan.md`: estado do lote 2.

Mapeamento do labeler: `layer/**` e `kernel/**` → `area/kernel-layer` (não há label próprio
para o módulo M1 de `kernel/`; o mais próximo é este); arquivos do índice e do `QUERY` →
`area/index-query`; `tools/**` e `common/**` → `area/tools`; testes, `vm/`, `Makefile`,
scripts de lint, `.devcontainer/` e `ci.yml` → `area/tests-vm`; arquivos do KASAN →
`area/kasan`; `docs/**`, `*.md` da raiz e `CITATION.cff` → `area/docs`; site → `area/site`;
`bench/**` e `docs/bench*.md` → `area/bench-m4`; `SECURITY.md`, `docs/security-roadmap.md`,
`codeql.yml` e `scorecard.yml` → `area/security`.

## Evidências

Comandos executados nesta sessão (host macOS, Apple Silicon):

- `sh scripts/checkpatch.sh` no host: hashes conferidos; código de saída 1 por causa de
  avisos. Linha de base: **0 erros, 14 avisos** em 7 arquivos — `kernel/main.c` 9
  (`LINE_SPACING`), `layer/index.c` 4 (2 `DEEP_INDENTATION`, 1 `ENOSYS`, 1
  `BLOCK_COMMENT_STYLE`), `layer/main.c` 1 (`BLOCK_COMMENT_STYLE`); os outros quatro limpos.
- `docker run rhysd/actionlint:1.7.7`: nenhum problema nos workflows novos nem no `ci.yml`
  alterado. Único achado, anterior a este lote: SC2251 em `kasan.yml` (ver Próximo passo).
- `docker run koalaman/shellcheck:v0.10.0 --shell=sh` (severidade padrão) em
  `scripts/checkpatch.sh` e `.devcontainer/post-create.sh`, e `--severity=warning` em todos os
  scripts de `make lint`: código de saída 0.
- Devcontainer local, com `npx @devcontainers/cli@0.80.1` e Docker Desktop (arm64; **não é um
  Codespace**, que é x86-64):
  - `devcontainer build`: `"outcome":"success"`.
  - `devcontainer up` construiu e iniciou o contêiner, e o `dockerd` interno ficou pronto, mas
    a CLI parou depois de "Container started" sem rodar o `postCreateCommand`; a causa não foi
    investigada. O `post-create.sh` foi então executado com `docker exec` (16 s).
  - Dentro do contêiner, numa cópia da árvore com índice Git próprio (a worktree aponta para um
    `.git` fora da montagem): `make test` OK (9 testes); `make lint` OK com `shellcheck`
    0.9.0; `make checkpatch` com os mesmos 14 avisos; `make bench-smoke` com
    `BENCH_SMOKE_PASS`; `make fuzz FUZZ_TIME=10` falhou por falta de
    `libclang_rt.asan-aarch64.a` e passou depois de acrescentar `libclang-rt-18-dev`; `make
    test` com ASan/UBSan (clang) OK.
  - `make vm-test` dentro do contêiner, via docker-in-docker: passou (ver a nota abaixo).
- Os workflows `scorecard.yml`, `labeler.yml` e `first-interaction.yml` não rodam neste PR
  (só em `main` ou com a definição do branch base). O `codeql.yml` e o passo de checkpatch
  rodam no próprio PR; os resultados estão nas notas abaixo.

Nenhum teste de kernel foi afetado: nada em `layer/`, `kernel/`, `vm/` ou no formato mudou.

## Limitações

- **Codespaces não medido.** Não foi criado nenhum Codespace nesta sessão (é um recurso cobrado
  na conta do mantenedor). As máquinas disponíveis para o repositório, segundo `gh api
  repos/studiomoob/llmoobfs/codespaces/machines`, são `basicLinux32gb` (2 núcleos, 8 GiB,
  32 GiB de disco) e `standardLinux32gb` (4 núcleos, 16 GiB, 32 GiB). Os números do
  devcontainer local acima não valem para Codespaces (outra arquitetura, emulação amd64 no
  `docker build`). Para medir, o mantenedor pode rodar:

  ```sh
  gh codespace create -R studiomoob/llmoobfs -b main -m basicLinux32gb
  # dentro do Codespace:
  df -h / /var/lib/docker; nproc; free -g
  /usr/bin/time -v make vm-test 2>&1 | tail -25
  df -h / /var/lib/docker; docker system df; du -sh build
  ```

  Com os números (tempo total, pico de disco, se a máquina de 2 núcleos basta), registrar uma
  nota neste arquivo, declarar `hostRequirements` e só então pôr o botão no README.
- **Scorecard**: o badge mostra "no data" até a primeira execução em `main`; a API
  `api.scorecard.dev` respondia 404 para o repositório em 2026-10-11.
- **Configurações do GitHub** (escritas negadas nesta sessão; ficam para o mantenedor). A
  permissão padrão do `GITHUB_TOKEN` está em `write` e Actions pode aprovar PRs, o que o
  Scorecard penaliza (Token-Permissions). Os workflows já declaram permissões próprias:

  ```sh
  gh api -X PUT repos/studiomoob/llmoobfs/actions/permissions/workflow \
    -f default_workflow_permissions=read -F can_approve_pull_request_reviews=false
  ```

- **OpenSSF Best Practices**: a inscrição em <https://www.bestpractices.dev> exige login do
  mantenedor no site; não foi feita.
- O `checkpatch.pl` baixado de git.kernel.org pode ser bloqueado por proteção anti-robô do
  cgit; nesse caso o script tenta o espelho no GitHub. Se os dois falharem, o passo falha como
  aviso e não bloqueia o PR.

Nota de 2026-10-11: o `make vm-test` dentro do devcontainer local (docker-in-docker, Docker
Desktop num Mac Apple Silicon com 12 CPUs e 8 GiB para a VM do Docker, imagem amd64 sob
emulação) terminou com código 0 e os marcadores `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` e
`MOOBFS_LAYER_PASS` (guest Debian 6.12.111 em QEMU/TCG). Tempo total de `make vm-test`: 6 min
02 s, incluindo a construção da imagem do zero no Docker interno. Espaço: imagem
`moobfs-m1-test` com 1,29 GB (`docker system df` no Docker interno) e `build/` com 294 MB. Isso
mostra que o devcontainer consegue rodar a suíte via docker-in-docker, mas **não são números de
Codespaces**: outra arquitetura de host e outra máquina; a medição em Codespaces continua
pendente.

Nota de 2026-10-11: no PR #30, primeira execução, passaram `Lint`, `Tools and sanitizers`,
`Native Linux VM` e `Analyze C (tools, common, bench)` (com o check `CodeQL`). No `Lint`, o
passo de aviso baixou os três arquivos do `checkpatch.pl`, conferiu os hashes ("OK") e listou
os mesmos avisos da linha de base; o job terminou verde, como previsto pelo
`continue-on-error`. O CodeQL (CLI 2.27.2, `security-extended`) registrou a análise de
`refs/pull/30/merge` com 0 resultados; a lista de arquivos extraídos não foi inspecionada além
do log do build.

Nota de 2026-10-11: renumerado de 0035 para 0039 ao trazer `main` para a branch. O registro
0035 (fase Q2a) entrou em `main` pelo PR #31, e as branches `m3/query-v2-q2b` a
`m3/query-v2-q2d` já usam 0036 a 0038.

Nota de 2026-10-12: o mantenedor aplicou o comando de permissões do `GITHUB_TOKEN`. Conferido com
`gh api repos/studiomoob/llmoobfs/actions/permissions/workflow`:
`default_workflow_permissions: read` e `can_approve_pull_request_reviews: false`. Seguem
pendentes a medição em Codespaces e a inscrição no OpenSSF Best Practices.

Nota de 2026-10-12: inscrição no OpenSSF Best Practices feita pelo mantenedor (projeto 15408,
<https://www.bestpractices.dev/projects/15408>), sem conceder ao aplicativo OAuth acesso às
organizações. A sessão coordenadora preparou as justificativas a partir do repositório e
preencheu o formulário no navegador do mantenedor, com as respostas pessoais
(`know_secure_design`, `know_common_errors`) dadas por ele. Resultado, conferido em
`https://www.bestpractices.dev/projects/15408.json`: `badge_level: passing`, 100%, obtido em
2026-10-12T12:03Z. São 60 critérios Met, 5 N/A (sem chaves, senhas, protocolo de rede ou
aleatoriedade criptográfica) e 2 Unmet, ambos só sugeridos: `test_most` (cobertura não medida)
e `crypto_call` (SHA-256 implementado em `common/hash.c`). O `static_analysis_fixed` foi
respondido depois da triagem do CodeQL (registro 0045, PR #42): 14 alertas fechados como
`fixed` na análise de `main` (`b19780b`) e 21 dispensados com justificativa; nenhum alerta do
CodeQL aberto. Badge acrescentado ao `README.md`. Segue pendente a medição em Codespaces.

## Próximo passo

Medir `make vm-test` num Codespace e decidir o botão. Zerar os 14 avisos do `checkpatch.pl` e
torná-lo obrigatório (item S5 de `docs/security-roadmap.md`). Corrigir em `kasan.yml` o
`! grep -q '^MOOBFS_VM_FAIL:'` que não é o último comando do passo: com `bash -e`, uma negação
fora de condição não interrompe o script (SC2251), então esse teste não reprova o passo sozinho;
hoje a ausência de `MOOBFS_KASAN_PASS` cobre o caso.
