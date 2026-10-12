# 0041 — Experiência do desenvolvedor no GitHub: lote 3 (vitrine)

Data: 2026-10-11. Branch: `community/dx-batch3`, criada a partir de `origin/main` (`34f175b`,
merge do PR #30). Estado: implementado no repositório; a consolidação das releases, o envio da
imagem de social preview e a criação das issues de tradução ficam com o mantenedor (escritas no
GitHub negadas nesta sessão; ver "Ações do mantenedor").

## Objetivo

Executar o lote 3 de [`docs/dx-plan.md`](../dx-plan.md): README com demonstração real e
quickstart antes do aviso de estágio experimental, imagem de social preview, índice de `docs/`
com o idioma de cada documento e destino único para as evidências de benchmark. Nada em
`layer/`, `kernel/` ou no formato mudou.

Numeração: conferidas `origin/main` (último 0039), todas as branches remotas (as
`m3/query-v2-q2b` a `q2d` usam 0036 a 0038) e as branches locais. O número 0040 ficou livre para
a próxima fase da série `m3/query-v2-*`, que vem reservando números em sequência; este registro
usa 0041. Achado: a branch local `codex/kernel-index-reuse-research` (checkout principal, não
publicada) tem um `0039-kernel-index-reuse-research.md`, que colide com o 0039 de `main` e
precisa ser renumerado antes de publicado.

## Decisões

Decisões do mantenedor, tomadas antes desta sessão: uma release única para as evidências de
benchmark; demonstração em texto com saída real, não GIF nem asciinema; social preview enviada
à mão; issues de tradução redigidas aqui e criadas pelo mantenedor.

| Tema | Decisão | Justificativa resumida |
| --- | --- | --- |
| Fonte da demonstração | Bloco novo em `vm/suite.sh`, depois dos três inferiores, sobre ext4, com prefixo `DEMO| ` e marcador `DEMO_PASS` | A suíte não imprimia uma sequência legível; o bloco tem asserções, então a saída do README é também um teste e o CI (incluindo o KASAN, que roda em PRs que tocam `vm/`) a reexecuta |
| Opções de montagem na demonstração | Padrão (`domain_depth=1`), `QUERY2` em diretório de domínio; v1 só como contraste (recusada ao usuário, respondida ao root) | Reflete o comportamento de `main` depois da fase Q2a |
| Preparação dos domínios | `mkdir`/`chown`/`chmod` dos diretórios no ext4 inferior antes de montar | Mudar o modo de um diretório de domínio com a camada montada põe o domínio em fallback até a remontagem (spec-query-v2 §3, Q2a); preparar antes evita isso sem uma remontagem no roteiro |
| Usuário que consulta | `query --uid 1001`, como no resto da suíte | Não há `su` nem `setpriv` no initramfs; o cliente de teste troca de uid antes da chamada, e o README diz isso |
| Trecho no README | As linhas `DEMO| ` sem o prefixo e sem o `CR` do console serial, omitida só a chamada final de `GET_INFO` (comando e saída inteiros) | Bloco curto antes do aviso; nenhuma linha de saída alterada; o registro guarda a sequência completa |
| Aviso experimental | Mesmo conteúdo em lista curta, logo depois do quickstart; fatos datados de 2026-10-11 | Pedido do mantenedor; ver "Fatos atualizados" |
| Release de evidências | Tag `bench-evidence`, pré-release, `--latest=false`, criada como rascunho, publicada só depois de conferir os anexos pela API | Não aparece como "Latest"; o rascunho tolera releases imutáveis no momento da criação |
| Manifesto das evidências | A tabela de `bench/evidence-release.md` (texto da release) é também o manifesto lido pelo script | Uma fonte só para nome, tamanho e SHA-256; o texto publicado e o que o script confere não divergem |
| Verificação depois do envio | Tamanho e digest pela API do GitHub e novo download pela URL pública, com `sha256` local | Exigência do mantenedor; o download confere o que um leitor recebe, não só o que a API declara |
| Apagar as pré-releases antigas | Só depois de todos os anexos conferidos; `gh release delete` sem `--cleanup-tag`, e o script confere que cada tag continua existindo | As tags marcam o commit de cada campanha |
| Novas campanhas | Subcomando `add` do mesmo script, regra em `docs/benchmark-harness.md` §7.1 e `bench/results/README.md` | Evita que uma campanha nova volte a criar uma pré-release própria |
| Social preview | SVG versionado em `docs/assets/`, script com `rsvg-convert`, PNG gerado | `docs/assets/` não é publicado no Pages, então o site não muda; cores e marca do site |
| Índice de `docs/` | `docs/README.md` em inglês, por grupo, com idioma e uma linha por documento | É a porta de entrada para quem chega; o README e o README.pt-BR apontam para ele |
| Traduções | Arquivo `<nome>.en.md` ao lado do original, que prevalece (CONTRIBUTING, política de idioma) | Convenção explícita nas issues para evitar propostas divergentes |

## Mudanças

- `vm/suite.sh`: bloco de demonstração (função `demo`, prefixo `DEMO| `, asserções, marcador
  `DEMO_PASS`). `docs/testing.md`: linha do marcador.
- `README.md`: frase de abertura, seção "See it run" com a saída real, "Quickstart" com quatro
  comandos, aviso experimental compacto logo abaixo, badges mantidos (inclusive o do
  Scorecard), link para o índice de `docs/`. Fora do topo, só o que contradizia `main`: "At a
  glance" e "How it works" passam a descrever o `QUERY2` por domínio; "Not implemented" e
  "Limitations" deixam de dizer que não há medição alguma; `DEMO_PASS` na lista de marcadores;
  duas linhas no mapa da documentação.
- `README.pt-BR.md`: espelho do topo e das mesmas correções; ganhou também a linha da fase Q2a na
  tabela de evidências, que faltava.
- `bench/evidence-release.md` (texto e manifesto da release), `scripts/bench-evidence-release.sh`
  (SPDX, POSIX `sh`), `bench/results/README.md` (índice das campanhas e regra para novas), e os
  sete `bench/results/*/README.md` com o link novo para o anexo, o nome da pré-release original
  e, no `profile-q7`, o SHA-256 que faltava.
- `docs/benchmark-harness.md` §7.1: onde ficam os artefatos de uma campanha.
- `docs/README.md` (índice), `docs/assets/social-preview.svg`, `docs/assets/social-preview.png`,
  `docs/assets/render-social-preview.sh`.
- `docs/dx-plan.md` (estado do lote 3), `CHANGELOG.md`, `docs/history/README.md`.

Os registros 0025, 0027 e 0029 continuam citando as pré-releases pelo nome, de propósito: eram
o destino na data em que foram escritos, e as tags permanecem.

### Fatos atualizados no aviso

| Antes (2026-10-10) | Agora (2026-10-11) | Fonte |
| --- | --- | --- |
| "the UAPI v1 (`QUERY`, `GET_INFO`) may change" | v1 e v2 podem mudar; com domínios ligados (padrão), a v1 é só para administradores | Registro 0035, `docs/spec-query-v2.md` |
| "no performance numbers have been measured (M4 pending)" | Só medições em nível de piloto numa VM de nuvem; M4 não feito; nenhuma alegação | `bench/results/`, registros 0025 a 0031 |
| "three kernel-side defects" (0015, 0016) | Defeitos encontrados e corrigidos pelos próprios testes e revisões, citando 0015, 0016 e 0035, sem contagem | O registro 0035 lista um `oops` e um erro de `rename` no código da Q2a antes do merge |
| README.pt-BR: "Sem versão lançada" | Só a tag de pre-release do fonte `v0.1.0-experimental` | Registro 0026 |

## Demonstração

Sequência completa, das linhas `DEMO| ` de `build/vm/console.log` (prefixo e `CR` removidos),
execução de 2026-10-11 descrita em "Evidências":

```text
# (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)
# mount -t moobfs_layer -o lower=/lower none /mnt/layer
# moobfs-attr set /mnt/layer/alice/cache.md kind string decision
# moobfs-attr set /mnt/layer/bob/deploy.md kind string decision
# moobfs-attr set /mnt/layer/team/style.md kind string decision
# moobfs-attr set /mnt/layer/team/salary.md kind string decision
# moobfs-attr list /lower/alice/cache.md
kind	string	decision
# stat -c '%A %u %n' /mnt/layer/team/*
-rw------- 1002 /mnt/layer/team/salary.md
-rw-r--r-- 0 /mnt/layer/team/style.md
# query --uid 1001 /mnt/layer/alice q2 kind eq string decision
16	cache.md
# query --uid 1001 /mnt/layer/team q2 kind eq string decision
18	style.md
# query --uid 1001 /mnt/layer/bob q2 kind eq string decision
open: Permission denied
# query --uid 1001 /mnt/layer kind eq string decision
MOOB_IOC_QUERY: Operation not permitted
# query /mnt/layer kind eq string decision
16	alice/cache.md
17	bob/deploy.md
18	team/style.md
19	team/salary.md
# query /mnt/layer info
uapi=1 format=1 name_max=64 string_max=1024 raw_max=4096 per_file=64
domain_depth=1 heterogeneous=1 fallback_domains=0
lower=ext4 caps=0x8 flags=0x10 budget=134217728 used=1050 entries=4 inodes=4
```

Leitura: `team/salary.md` (0600, dono 1002, num domínio 0755 de root) não satisfaz a regra de
homogeneidade e fica fora da partição; por isso a Alice não o vê, e o Bob também não o veria
pelo `QUERY2` (falha segura da Q2a). O `GET_INFO` ainda informa `uapi=1`: o campo de versão não
mudou na Q2a.

## Releases de benchmark

Inventário em 2026-10-11 (`gh release list`, `gh release view --json assets`): **sete**
pré-releases `bench-2026-10-11-*`, não oito como dizia o pedido, mais a `v0.1.0-experimental`.
Nenhuma é rascunho; nenhuma é imutável. Os sete anexos têm nomes distintos, então cabem numa só
release. Tamanhos de 1,09 MB a 129,4 MB, 162,4 MB no total; os SHA-256 publicados pelo GitHub
(`digest`) coincidem com os citados nos `bench/results/*/README.md` (o do `profile-q7` não
estava citado e foi acrescentado).

`scripts/bench-evidence-release.sh` (padrão `--dry-run`):

- `migrate`: baixa cada anexo da pré-release original para `build/bench-evidence/download/` e
  confere tamanho e SHA-256 com a tabela; cria `bench-evidence` como rascunho se não existir;
  envia o que falta (um anexo já presente só é aceito se for idêntico; nunca usa `--clobber`);
  confere tamanho e digest pela API; publica; baixa de novo pela URL pública e confere; só então
  apaga as sete pré-releases e confere que cada tag continua no remoto.
- `add ARQUIVO...`: para campanhas novas; exige uma linha na tabela, confere o arquivo local,
  envia, confere pela API e pela URL pública e atualiza o texto da release a partir do arquivo.

## Social preview

`docs/assets/social-preview.svg` → `social-preview.png` (1280×640, 86 684 bytes) com
`rsvg-convert` 2.62.3 (Homebrew). Conteúdo: marca do site, "MoobFS", "Linux filesystem layer for
LLM-agent memory", uma frase sobre o que a camada faz e a faixa "Experimental research project ·
GPL-2.0 · no FUSE · run it in a disposable VM". Nenhuma afirmação de desempenho. Conferida
reduzida a 400×200: título, subtítulo e faixa legíveis. A fonte vem do sistema (Arial ou
Helvetica, com DejaVu Sans como alternativa), então outro computador pode gerar um PNG
ligeiramente diferente.

## Índice de `docs/` e traduções

`docs/README.md` lista os 31 documentos da raiz de `docs/` (conferido por script: nenhum
ausente) e aponta `decisions/`, `history/`, `bench/results/` e `assets/`. Documentos de entrada só em português, escolhidos para issues de tradução: a
especificação do `QUERY2` (interface padrão desde a Q2a), a especificação do M2, as ADRs 0007 e
0009, o roadmap de segurança (citado pela issue #16), o harness de benchmark e o
`ai-workflow.md`. Comandos para o mantenedor (rótulos como em `docs/labels.md`: `good first
issue` junto de `difficulty/good-first-issue`):

```sh
mk() {
    gh issue create -R studiomoob/llmoobfs --label "good first issue" --label "help wanted" \
        --label area/docs --label difficulty/good-first-issue --label priority/p2 \
        --title "$1" --body-file -
}

mk 'Translate docs/spec-query-v2.md (domain-scoped QUERY2, UAPI v2) to English' <<'EOF'
`docs/spec-query-v2.md` specifies `MOOB_IOC_QUERY2`, the query a client uses by default since phase Q2a (journal 0035): permission domains, the homogeneity rule, the ioctl contract and the phases Q2a to Q2e. It exists only in Portuguese, so anyone writing a client or reviewing the layer has to read it through a machine translation.

**Task.** Add `docs/spec-query-v2.en.md`, an English translation of the current file.
- First line: "Translation of `docs/spec-query-v2.md` at commit `<sha>`. The Portuguese original prevails." (see the language policy in CONTRIBUTING.md).
- Keep the status words exactly: proposal, accepted, implemented, and the dated "Implementação Q2a" notes that record how the code differs from the text.
- Do not add or soften claims. Keep the section numbers, so links like §3 still work.
- Add the file to `docs/README.md` with language EN.

No kernel knowledge is needed beyond reading carefully; ask in the issue if a sentence is ambiguous. Sign off your commit (DCO).
EOF

mk 'Translate docs/spec-m2.md (stacked memory layer) to English' <<'EOF'
`docs/spec-m2.md` is the specification of the stacked layer that the code implements (pass-through over ext4, btrfs and XFS, typed `user.moob.*` attributes, the RAM index and `QUERY`). Newcomers are pointed to it from the README, but it exists only in Portuguese. `docs/architecture-overview.md` (English) describes the code; it does not replace the specification.

**Task.** Add `docs/spec-m2.en.md`.
- First line: "Translation of `docs/spec-m2.md` at commit `<sha>`. The Portuguese original prevails."
- Keep "proposal" versus "implemented" exactly as the original says, and keep section numbers.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate ADR-0007 and ADR-0009 to English' <<'EOF'
Two decisions explain most of the current design: ADR-0007 (MoobFS as a layer stacked over a mature filesystem) and ADR-0009 (the attribute index stays in the kernel, partitioned by permission domain). Both are in Portuguese only.

**Task.** Add `docs/decisions/0007-stacked-memory-layer.en.md` and `docs/decisions/0009-query-role-and-user-space-index.en.md`.
- First line of each: "Translation of `<file>` at commit `<sha>`. The Portuguese original prevails."
- Keep the status line, the alternatives and the dated maintainer decisions as they are; do not summarise them away.
- Update the "Decisions and history" row of `docs/README.md`.

ADR-0009 is long (about 300 lines); a pull request with ADR-0007 alone is welcome. Sign off your commits (DCO).
EOF

mk 'Translate docs/security-roadmap.md to English' <<'EOF'
`docs/security-roadmap.md` lists the security and robustness items S1 to S11 (threat model, xfstests, syzkaller, hostile lower filesystem, kernel lint, disclosure, fuzzing, hardening, resource limits, module signing). Review requests such as #16 refer to it, and security reviewers are the readers least likely to read Portuguese.

**Task.** Add `docs/security-roadmap.en.md`.
- First line: "Translation of `docs/security-roadmap.md` at commit `<sha>`. The Portuguese original prevails."
- Keep the S-numbers and the state of each item ("a fazer", done, proposal) exactly.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate docs/benchmark-harness.md (M4 harness specification) to English' <<'EOF'
`docs/benchmark-harness.md` specifies the M4 benchmark harness in `bench/`: dataset generator, SQLite baselines and equivalence, latency records, statistics, and (§7.1) where campaign artefacts go. The campaign results in `bench/results/` are in English, but the specification behind them is only in Portuguese.

**Task.** Add `docs/benchmark-harness.en.md`.
- First line: "Translation of `docs/benchmark-harness.md` at commit `<sha>`. The Portuguese original prevails."
- Translate numbers and units as they are; add no performance statement.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate docs/ai-workflow.md (AI-assisted development rules) to English' <<'EOF'
`docs/ai-workflow.md` (26 lines) explains how AI-assisted work is governed in this repository: the shared rules in `AGENTS.md`, and how Codex, Claude Code and Cursor read them. CONTRIBUTING.md welcomes AI-assisted contributions, so this page matters to newcomers, but it is only in Portuguese.

**Task.** Add `docs/ai-workflow.en.md`.
- First line: "Translation of `docs/ai-workflow.md` at commit `<sha>`. The Portuguese original prevails."
- Add the file to `docs/README.md` with language EN.

A short first contribution. Sign off your commit (DCO).
EOF
```

## Evidências

Comandos executados nesta sessão (host macOS, Apple Silicon, Docker 29.8.2):

- `make vm-test`: código de saída 0 em 6 min 21 s, com a imagem Docker construída do zero.
  Guest Debian 6.12.111+deb13-amd64 em QEMU 10.0.13 (TCG). Marcadores: `MOOBFS_VM_PASS`,
  `QUERY2_PASS`, `ISOLATION_PASS`, `BENCH_LAYER_PASS` e `LAYER_LOWER_PASS` para ext4, btrfs e
  xfs, `DEMO_PASS`, `ACCEPTANCE_CLIENTS_PASS`, `POWER_LOSS_REPLAY_PASS`,
  `IO_ERROR_INJECTION_PASS`, `MOOBFS_ACCEPTANCE_PASS` e `MOOBFS_LAYER_PASS`. A saída da
  demonstração está acima.
- `docker run koalaman/shellcheck:v0.10.0 --shell=sh --severity=warning vm/suite.sh` e, na
  severidade padrão, `scripts/bench-evidence-release.sh`: código 0.
- `scripts/bench-evidence-release.sh` (simulação): baixou os sete anexos (20 s), conferiu
  tamanho e SHA-256 de cada um contra a tabela e listou as escritas que faria (criar o rascunho,
  sete envios, sete exclusões). `add` com um arquivo fora da tabela: recusado; com um anexo
  baixado: conferido e envio listado; com uma cópia de um byte a mais: recusada pelo tamanho;
  argumento desconhecido: recusado.
- `docs/assets/render-social-preview.sh`: PNG 1280×640 (`magick identify`), conferido
  visualmente em tamanho real e reduzido.
- O bloco de comandos `gh issue create` acima, executado com `gh` substituído por uma função
  que imprime os argumentos: seis chamadas, cinco rótulos cada, títulos e corpos completos.
- Outros resultados (lint, SPDX, CI) estão nas notas abaixo.

## Limitações

- **O `--apply` não foi executado**, porque escritas no GitHub estão negadas nesta sessão. Só o
  caminho de simulação e os downloads foram testados; a criação do rascunho, os envios, a
  verificação pela API e pela URL pública e as exclusões rodam pela primeira vez com o
  mantenedor. O script supõe que `gh release view <tag>` encontra a release ainda em rascunho; isso
  não foi exercitado aqui.
- Até o `--apply`, os links novos em `bench/results/*/README.md`, `bench/results/README.md` e
  nos READMEs respondem 404. Por isso o `--apply` deve rodar antes do merge deste PR (a partir
  da branch, porque o script lê `bench/evidence-release.md` localmente).
- A release `bench-evidence` é recente e aparecerá acima da `v0.1.0-experimental` na lista de
  releases; marcada como pré-release e `--latest=false`, não vira "Latest". A lista passa de
  oito entradas para duas.
- A demonstração usa o cliente de teste `query`, não uma ferramenta para usuários; o README
  diz isso. Ela não mede tempo nem mostra nada além dos quatro arquivos.
- A imagem de social preview depende das fontes do sistema que a gera.

## Ações do mantenedor

1. Releases (antes do merge, a partir da branch `community/dx-batch3`):

   ```sh
   scripts/bench-evidence-release.sh            # simulação: confere downloads e lista escritas
   scripts/bench-evidence-release.sh --apply    # cria, envia, confere e só então apaga as sete pré-releases
   ```

   Opcional: `--target <sha>` fixa o commit da tag `bench-evidence` (padrão: `main`).
2. Social preview: Settings → General → Social preview → Edit → Upload an image →
   `docs/assets/social-preview.png`.
3. Issues de tradução: rodar os comandos `gh issue create` da seção anterior.

Nota de 2026-10-11 (revisão da sessão coordenadora): o parágrafo que apresenta a demonstração
dizia que a Alice recebe em `team/` "só o arquivo que pode ler", sugerindo um filtro por
chamador. Pela regra de homogeneidade (`docs/spec-query-v2.md` §3), o `salary.md` fica fora da
partição de `team/` para qualquer chamador; o `QUERY2` não verifica permissão por resultado. Os
dois READMEs passaram a explicar isso. Saída da demonstração e testes inalterados.

Nota de 2026-10-12: as três ações do mantenedor foram feitas e conferidas pela sessão
coordenadora.

- Releases: antes do merge do PR #33, a sessão rodou a simulação de
  `scripts/bench-evidence-release.sh` num worktree da branch. Os sete arquivos foram baixados e
  conferidos contra o manifesto, e as escritas listadas. Depois o mantenedor rodou `--apply`.
  Conferido com `gh release list` e `gh release view bench-evidence`:
  - restam duas releases, `bench-evidence` (pré-release, publicada) e `v0.1.0-experimental`;
  - os sete anexos têm nome, tamanho e SHA-256 iguais aos de `bench/evidence-release.md`
    (comparação por `diff`);
  - as sete tags `bench-2026-10-11-*` continuam existindo.

  Os sete links `releases/download/bench-evidence/...` do repositório respondem 200, e nenhum
  link aponta para as releases antigas. A `bench-evidence` aparece acima da
  `v0.1.0-experimental` por ser mais recente; nenhuma das duas é "Latest".
- Issues de tradução: criadas como #35 a #40 a partir dos comandos da seção acima, cada uma com
  `good first issue`, `help wanted`, `area/docs`, `difficulty/good-first-issue` e `priority/p2`.
- Social preview: imagem enviada pelo mantenedor. A API mostra
  `usesCustomOpenGraphImage: true`, e a imagem publicada pelo GitHub tem o mesmo SHA-256 de
  `docs/assets/social-preview.png`.

Nota de 2026-10-12: o site passa a usar a mesma imagem em `og:image` e `twitter:card`
(`summary_large_image`) nas quatro páginas; a imagem foi copiada para
`site/social-preview.png`, porque o Pages publica só `site/`. `python3 scripts/check-site.py`:
"Site links valid: 4 HTML pages". O plano foi encerrado na seção 4 de `docs/dx-plan.md`.

## Próximo passo

Nenhum dentro do lote 3. Pendências restantes do plano (Codespaces, assinatura de releases) e os
repasses à sessão da Q2 estão na seção 4 de `docs/dx-plan.md`.
