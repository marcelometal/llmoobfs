# Plano de experiência do desenvolvedor no GitHub

Data: 2026-10-11. Estado em 2026-10-12: **plano concluído** (seção 4); pendências opcionais e
repasses na seção 4. Histórico do estado: proposta aprovada pelo mantenedor; lote 1 concluído (registro
[0032](history/0032-dx-batch1.md)); lote 2 implementado; selo OpenSSF Best Practices "passing" obtido em
2026-10-12; pendente só a medição em Codespaces (registro [0039](history/0039-dx-batch2.md)); lote 3
concluído, com as ações do mantenedor aplicadas em 2026-10-12 (registro
[0041](history/0041-dx-batch3.md)). Complementa [`community-plan.md`](community-plan.md)
(políticas, templates, CI de lint e DCO) e [`seo-geo-plan.md`](seo-geo-plan.md) (descoberta),
que já foram executados em grande parte e não são repetidos aqui.

Objetivo: tornar o repositório fácil de encontrar, de entender em poucos minutos e de
contribuir sem atrito para quem chega pela primeira vez, sem enfraquecer as cautelas de
estágio experimental nem a regra de que o módulo só roda em VM descartável.

## 1. Diagnóstico (verificado em 2026-10-11)

Já existe: CoC, CONTRIBUTING, SECURITY, GOVERNANCE, ROADMAP, cinco issue forms, template de PR,
DCO e SPDX no CI, 27 labels, Discussions ativado (categorias padrão), site no Pages, `main`
protegida com dois checks obrigatórios, Dependabot para actions, actions fixadas por SHA e 13
issues abertas para colaboradores.

Lacunas encontradas:

| Tema | Achado | Evidência |
| --- | --- | --- |
| Descoberta de tarefas | Issues iniciais usam só `difficulty/good-first-issue`; a página `/contribute` do GitHub, a busca e agregadores (goodfirstissue.dev, up-for-grabs) usam o label exato `good first issue`; `help wanted` não é usado | `gh issue list`, `gh label list` |
| Releases | Seis pré-releases (sete em 2026-10-11) "Benchmark evidence" aparecem antes de `v0.1.0-experimental`; nenhuma release é "Latest" | `gh release list` |
| Social preview | Imagem gerada pelo GitHub, sem imagem própria | `usesCustomOpenGraphImage: false` |
| README | O banner de cautelas vem antes de qualquer demonstração; não há quickstart curto nem demo | leitura de `README.md` |
| Discussions | Zero discussões; nenhuma issue fixada | GraphQL `discussions.totalCount` |
| Ambiente | `make vm-test` precisa só de Docker sem privilégio e usa TCG, mas não há devcontainer; quem usa macOS/Windows depende de Docker Desktop local | `docs/development.md` §1 e §3 |
| Comandos locais | Não há `make help` nem `make lint`; o lint existe só no CI | `docs/development.md` §2 |
| Estilo do kernel | Sem `checkpatch.pl` nem `.clang-format` | árvore do repositório |
| Revisão | Sem `CODEOWNERS` | árvore do repositório |
| Segurança do repositório | Secret scanning e push protection desligados (gratuitos em repositório público); exclusão de branch após merge desligada | `gh api repos/...` |
| Confiança externa | Sem OpenSSF Scorecard, selo OpenSSF Best Practices, CodeQL ou `CITATION.cff` | árvore e configurações |
| Idioma | Documentos de entrada misturam português e inglês sem índice por idioma | `docs/` |

Nota: a API `community/profile` informa `issue_template: null`, mas a página "Community
standards" no navegador marca os templates como presentes. A API não reconhece issue forms
YAML; nada a corrigir.

## 2. Lotes

### Lote 1 — Ajustes rápidos (esta rodada)

1. Aplicar `good first issue` junto de `difficulty/good-first-issue`, e `help wanted` nas issues
   abertas a colaboradores; documentar a regra em `docs/labels.md`.
2. Ligar secret scanning e push protection; ligar exclusão automática de branch após merge.
3. `.github/CODEOWNERS` apontando o mantenedor (pedido de revisão automático; não bloqueia,
   pois `require_code_owner_reviews` segue desligado).
4. `CITATION.cff` (botão "Cite this repository").
5. `make help` e `make lint`; o job `Lint` do CI passa a chamar `make lint`, para que o comando
   local e o CI sejam o mesmo. A verificação de SPDX vai para `scripts/check-spdx.sh`, que a
   issue #15 pode estender a shell e Python.
6. Uma discussão de boas-vindas em Discussions e três issues fixadas.

### Lote 2 — Ferramentas

- Devcontainer com docker-in-docker e botão "Open in Codespaces". Antes de anunciar: medir em
  Codespaces o tempo de `make vm-test`, o disco usado e a máquina mínima; registrar os números
  ou a falha.
- OpenSSF Scorecard (workflow e badge) e inscrição no OpenSSF Best Practices.
- CodeQL para as ferramentas em C (`tools/`, `common/`, `bench/`).
- `checkpatch.pl --no-tree` em `layer/` e `kernel/` no job `Lint`, começando como aviso.
- Labeler por caminho (`area/*`) e mensagem de primeira contribuição.

Estado em 2026-10-11 (registro [0039](history/0039-dx-batch2.md)):

| Item | Estado |
| --- | --- |
| Devcontainer | Arquivo pronto; construído localmente com a CLI de Dev Containers (não é um Codespace). **Pendente**: medir `make vm-test` num Codespace real; até lá, sem botão no README e sem `hostRequirements` |
| OpenSSF Scorecard | Workflow e badge prontos; só roda em `main` (push e agenda), então o primeiro resultado sai depois do merge. **Pendente (mantenedor)**: inscrição no OpenSSF Best Practices |
| CodeQL | Workflow com build manual de `make tools attr-tools bench-tools` |
| `checkpatch.pl` | `make checkpatch`, passo de aviso (`continue-on-error`) no job `Lint`; linha de base: 0 erros e 14 avisos em 7 arquivos |
| Labeler e primeira contribuição | Workflows prontos; por usarem `pull_request_target`, só passam a valer depois do merge |

### Lote 3 — Vitrine

- README: quickstart e demonstração (GIF ou asciinema de `QUERY`) antes do banner, que
  continua integral, mas compacto.
- Imagem de social preview (1280×640). Só pode ser enviada pela interface web.
- Índice de `docs/` com o idioma de cada documento; tradução dos documentos de entrada que
  ainda estejam só em português.
- Destino das evidências de benchmark: manter como pré-releases, consolidar numa única release
  de evidências ou mover para outro local. **Decisão do mantenedor**; qualquer mudança
  preserva os anexos e é registrada.

Estado em 2026-10-11 (registro [0041](history/0041-dx-batch3.md)). Decisões do mantenedor:
release única para as evidências; demonstração em texto com saída real, não GIF nem asciinema.

| Item | Estado |
| --- | --- |
| README | Frase de abertura, demonstração real (bloco `DEMO| ` de `vm/suite.sh`, `QUERY2` por domínio, gerado por `make vm-test`), quickstart de quatro comandos, aviso experimental integral e compacto logo abaixo, com fatos de 2026-10-11; espelhado no `README.pt-BR.md` |
| Social preview | `docs/assets/social-preview.{svg,png}` e script de renderização. **Pendente (mantenedor)**: envio em Settings → Social preview |
| Índice de `docs/` | `docs/README.md` com idioma e descrição de cada documento. **Pendente (mantenedor)**: criar as seis issues de tradução cujos comandos estão no registro 0041 |
| Evidências de benchmark | Release única `bench-evidence` preparada: texto e manifesto (`bench/evidence-release.md`), `scripts/bench-evidence-release.sh` (simulação testada), links atualizados, regra para novas campanhas. **Pendente (mantenedor)**: `scripts/bench-evidence-release.sh --apply`, antes do merge |

## 3. Fora do escopo

- Bot de "stale": fecha automaticamente issues de quem chega e não combina com um projeto pequeno.
- Wiki: a documentação versionada fica em `docs/`.
- Qualquer alteração em `layer/`, `kernel/` ou no formato.

## 4. Encerramento (2026-10-12)

| Lote | Resultado | Onde |
| --- | --- | --- |
| 1 | `make help` e `make lint`, CODEOWNERS, `CITATION.cff`, labels de descoberta, issues fixadas, discussão de boas-vindas (#29), secret scanning e push protection | PR #27, registro [0032](history/0032-dx-batch1.md) |
| 2 | Devcontainer, OpenSSF Scorecard, CodeQL (30 alertas triados: 14 corrigidos, 21 dispensados com justificativa, nenhum aberto), `checkpatch.pl` como aviso, labeler, saudação de primeira contribuição, token do Actions só leitura, `virtme-ng` instalado por hash no KASAN; selo **OpenSSF Best Practices passing** (projeto 15408) | PRs #30, #42, #44 e #45; registros [0039](history/0039-dx-batch2.md), [0045](history/0045-codeql-triage.md) e [0046](history/0046-kasan-pip-hashes.md) |
| 3 | README com demonstração real e quickstart, release única `bench-evidence`, social preview (GitHub e `og:image` do site), índice de `docs/` por idioma, issues de tradução #35 a #40 | PRs #33, #41 e o PR deste encerramento; registro [0041](history/0041-dx-batch3.md) |

Primeira contribuição externa: PR #43, de @dfbustosus, tradução de `docs/ai-workflow.md` (issue
#40, uma das issues abertas no lote 3), mergeado em 2026-10-12 depois de passar pelo labeler, pela
saudação automática, pelo CODEOWNERS e pela aprovação de CI exigida para quem contribui pela
primeira vez a partir de um fork.

OpenSSF Scorecard em 2026-10-12, antes de refletir o selo: 6,9. Notas que sobem sozinhas:
CII-Best-Practices (selo obtido), SAST (commits novos passam pelo CodeQL), Maintained (o
repositório completa 90 dias). Notas que dependem de pessoas ou de decisão: Code-Review (exige
aprovação de uma segunda pessoa nos PRs) e Signed-Releases.

Pendências que ficam fora do plano:

- **Opcional, mantenedor:** medir `make vm-test` num Codespace (tempo, disco, máquina mínima)
  antes de anunciar o botão "Open in Codespaces"; comandos no registro 0039. Usa a cota paga do
  Codespaces.
- **Decisão do mantenedor:** assinar as próximas releases (Scorecard Signed-Releases).
- **Repassadas à sessão da fila Q2**, por mexerem em código ou no harness que ela usa:
  zerar os 14 avisos do `checkpatch.pl` em `layer/` e `kernel/` e torná-lo obrigatório (item S5
  de `docs/security-roadmap.md`); corrigir o `qsort(NULL, 0, ...)` de
  `tools/moobfs-index.c` apontado pelo UBSan (registro 0045); acrescentar `make bench-smoke` com
  sanitizers ao CI; corrigir em `kasan.yml` a negação `! grep` que não reprova o passo (SC2251,
  registro 0039).

