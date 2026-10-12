# MoobFS

**Filesystem nativo experimental para Linux, voltado à memória e ao contexto de LLMs.**

O MoobFS é um módulo do kernel Linux (GPL-2.0, sem FUSE) que se empilha sobre um diretório de
ext4, btrfs ou XFS, valida atributos tipados `user.moob.*` em arquivos comuns e responde a
consultas por atributo a partir do kernel, devolvendo só os arquivos que quem chama pode ler. É
um projeto de pesquisa sobre armazenamento para a memória de agentes de LLM.

[English](README.md) · [Site](https://studiomoob.github.io/llmoobfs/) · [Índice da documentação](docs/README.md) · [FAQ (EN)](docs/faq.md) · [Contribuição](CONTRIBUTING.md) · [Roadmap](ROADMAP.md)

## Veja funcionando

Cada diretório de primeiro nível da montagem é um domínio de permissão. O root marca quatro
arquivos de memória com um atributo tipado; depois a Alice (uid 1001) consulta. Ela recebe a
própria memória e, no domínio compartilhado `team/`, só o `style.md`. O `salary.md` não aparece
porque um domínio só indexa arquivos que todos os que podem entrar nele conseguem ler (a regra de
homogeneidade de [spec-query-v2](docs/spec-query-v2.md) §3); legível só pelo Bob, ele fica fora do
índice de `team/` para qualquer chamador, inclusive o Bob e o root, e arquivos privados devem ficar
num domínio próprio. O domínio do Bob e o `QUERY` da montagem inteira (v1, para administradores)
são recusados a ela:

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
```

Saída real de `make vm-test` em 2026-10-11 (Debian 6.12.111 no QEMU, inferior ext4): as linhas
`DEMO| ` de `build/vm/console.log` sem o prefixo, deixando de fora a chamada final de `GET_INFO`.
As linhas que começam com `#` são comandos executados como root. `query` é o cliente de teste em
[`tests/query.c`](tests/query.c), porque ainda não há CLI; `--uid 1001` troca para esse usuário
antes da chamada, e a forma `q2` é o `MOOB_IOC_QUERY2` sobre um diretório de domínio. O roteiro é
o bloco de demonstração de [`vm/suite.sh`](vm/suite.sh).

## Início rápido

Com Docker em execução (no Apple Silicon a VM roda sob emulação de CPU e demora mais):

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test                          # compila ferramentas e módulos e inicia uma VM QEMU descartável
grep '^DEMO| ' build/vm/console.log   # a sessão acima, na sua própria execução
```

Nunca carregue os módulos no seu host. Ambiente em [docs/development.md](docs/development.md); o
que cada marcador de sucesso prova está em [docs/testing.md](docs/testing.md) (em inglês).

> **Experimental. Só uma tag de pre-release do fonte (`v0.1.0-experimental`); não use em
> produção.** Fatos em 2026-10-11:
> - **Uma série de kernel**, em VM: Debian 6.12.111 no QEMU e upstream 6.12.112 sob
>   KASAN/UBSAN/lockdep; inferiores apenas ext4, btrfs e XFS.
> - O índice de atributos vive **só em RAM**, limitado por `index_budget`, e é reconstruído a
>   cada montagem.
> - Montar exige **`CAP_SYS_ADMIN` no namespace de usuário inicial**; montagens em user
>   namespace e idmapped são recusadas.
> - **As UAPIs v1 e v2 podem mudar**, sem promessa de compatibilidade. Com os domínios ligados (o
>   padrão desde a fase Q2a), o `QUERY` v1 é só para administradores.
> - **Nenhuma alegação de velocidade ou eficiência.** Só existem medições em nível de piloto
>   numa VM de nuvem ([bench/results/](bench/results/README.md)); o M4 não foi feito.
> - **Sem auditoria externa.** Os próprios testes e revisões do projeto encontraram e corrigiram
>   defeitos no lado do kernel (registros [0015](docs/history/0015-phase-2e-investigation.md),
>   [0016](docs/history/0016-kasan-lockdep-harness.md),
>   [0035](docs/history/0035-phase-q2a-domain-query.md)).
> - Reflink, deduplicação e durabilidade vêm do filesystem inferior, não do MoobFS.
>
> Contribuições são bem-vindas ([CONTRIBUTING.md](CONTRIBUTING.md)). Todo o repositório é em
> inglês, inclusive os documentos de projeto e o diário técnico; este arquivo é uma cópia de
> cortesia do README ([índice da documentação](docs/README.md)).

## Em resumo

**O que é.** Um módulo nativo do kernel Linux, `moobfs_layer.ko`, montado como camada
empilhada sobre um diretório de um filesystem maduro (ext4, btrfs ou XFS). Memórias são
arquivos comuns; seus metadados são atributos estendidos tipados (`user.moob.*`) que o módulo
valida na escrita e indexa em RAM, particionados por domínio de permissão (por padrão, cada
diretório de primeiro nível). Um ioctl `QUERY2` sobre um diretório de domínio responde
predicados conjuntivos sobre atributos a partir do kernel, devolvendo só arquivos que todos os
que entram no domínio podem ler; o `QUERY` antigo, sobre a montagem inteira, verifica cada
resultado pelas permissões POSIX e ACLs e, com os domínios ligados, é para administradores. Tudo continua legível com ferramentas Unix comuns
sem a camada. O formato de imagem somente leitura do M1, com conteúdo físico compartilhado, é
mantido como regressão.

**O que não é.** Não é FUSE nem daemon: não há servidor em espaço de usuário, socket ou
processo em segundo plano. Não há busca semântica ou vetorial no kernel: ranking, embeddings e
montagem de contexto são do cliente. Não substitui banco de dados; para qualquer uso além de
uma VM descartável, hoje, SQLite ou arquivos comuns continuam sendo a recomendação
([avaliação de valor](docs/value-assessment.md)). Não há persistência do índice, snapshots nem
transações próprias; durabilidade, reflink e dedup são do filesystem inferior.

**Como verificar qualquer afirmação daqui.** Rode `make vm-test` e leia `build/vm/console.log`
([guia de testes](docs/testing.md)); prepare o ambiente com [docs/development.md](docs/development.md).

## Objetivo do projeto

O MoobFS busca oferecer uma base de armazenamento, no nível de filesystem, para memória
persistente e contexto consumidos por agentes baseados em LLMs: preservar memórias como
observações, decisões e registros de tarefas entre sessões, compartilhar conteúdo repetido e,
futuramente, manter revisões que os agentes possam recuperar para compor seu contexto.

O filesystem armazena e expõe as memórias como arquivos. A aplicação do agente seleciona o
material relevante e monta o contexto enviado à LLM. Recuperação semântica e montagem de
contexto ainda precisam ser desenhadas e integradas; o MoobFS não fornece essas capacidades
hoje nem amplia a janela de contexto do modelo.

## Como funciona

Escrever uma memória é um `write` POSIX comum mais `setxattr`; a camada valida a codificação
(byte de tipo mais carga) antes de o xattr chegar ao inferior e atualiza o índice. Encontrar
memórias é uma chamada `QUERY2` sobre um diretório de domínio: até quatro predicados sobre
atributos em conjunção, respondidos pela partição daquele domínio depois de uma verificação de
permissão no diretório. Um arquivo que nem todos os que entram no domínio podem ler fica fora da
partição ([spec-query-v2](docs/spec-query-v2.md) §3). O cliente então abre o arquivo
normalmente. Desmontada a camada,
o diretório inferior é uma árvore comum que `grep`, `getfattr`, `git` e `rsync` leem sem o
módulo. Detalhes com citações do código (em inglês):
[visão da arquitetura](docs/architecture-overview.md); o fluxo em diagrama:
[site/plan.html](https://studiomoob.github.io/llmoobfs/plan.html#flow).

## O que está implementado, com evidência

"Testado em" nomeia o kernel e os inferiores que de fato rodaram; nada aqui significa prontidão
para produção. Desde a [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) o MoobFS é uma
camada empilhada nativa sobre um filesystem maduro; o experimento M1 está congelado e roda como
regressão na mesma VM.

| Item | Registro | Testado em |
| --- | --- | --- |
| M1, imagem somente leitura: criador offline, verificador, módulo nativo, dois inodes compartilhando um conteúdo (FIEMAP), 15 imagens malformadas rejeitadas | [0002](docs/history/0002-native-read-milestone.md), [formato](docs/format-m1.md) | Debian 6.12.111 (QEMU), loop |
| Fases 2a–2b: passagem empilhada (`mount -t moobfs_layer -o lower=<dir>[,nolinks]`): arquivos, diretórios, rename, links, xattr, mmap, fsync; reflink/dedup/`copy_file_range` delegados, FIEMAP; `fsx` | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [0012](docs/history/0012-phase-2b-full-passthrough.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase 2c: atributos tipados `user.moob.*` validados no kernel; `moobfs-attr`, `moobfs-check`; testes unitários no host; alvo libFuzzer | [0013](docs/history/0013-phase-2c-typed-attributes.md) | Testes no host (Linux/macOS), VM sobre os três inferiores |
| Fase 2d: índice em memória, `QUERY` e `GET_INFO` (UAPI v1), respostas iguais ao oráculo `moobfs-index`, inclusive após rename, unlink, removexattr e remontagem; `noindex`, `index_budget` | [0014](docs/history/0014-phase-2d-index-query.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Fase 2e: aceitação com git, SQLite em WAL concorrente, cliente de memória próprio; replay de queda de energia com `dm-log-writes`; erros de E/S com `dm-flakey`; dois defeitos da camada encontrados e corrigidos | [0015](docs/history/0015-phase-2e-investigation.md) | Debian 6.12.111; só ext4 |
| Harness KASAN/UBSAN/lockdep com a mesma suíte; terceiro defeito encontrado e corrigido | [0016](docs/history/0016-kasan-lockdep-harness.md) | Upstream 6.12.112, virtme-ng |
| M3 fase 3b: autorização POSIX por resultado no `QUERY`; dois uids, dois namespaces, chmod e ACLs, remontagem, montagens idmapped e em user namespace recusadas, corrida de 5 s de chmod/rename/unlink contra `QUERY` | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 | Debian 6.12.111 e KASAN 6.12.112; ext4, btrfs, XFS |
| Fase Q2a: `QUERY2` por domínio (UAPI v2) sobre um diretório de domínio, índice particionado por domínio e regra de homogeneidade, v1 restrita a administradores enquanto `domain_depth` estiver ligado, `EXDEV` para diretórios entre domínios; igual ao `moobfs-index --domain` (desempenho e teste do canal de tempo ainda não medidos) | [0035](docs/history/0035-phase-q2a-domain-query.md), [spec-query-v2](docs/spec-query-v2.md) | Debian 6.12.111; ext4, btrfs, XFS |

Não implementado: índice persistente, montagens em user namespace ou idmapped, leituras em
lote, cursor de mudanças, revisões, empacotamento, UAPI estável, CLI, xfstests completo e as
medições do M4 (só existem campanhas em nível de piloto, em [bench/results/](bench/results/README.md)). Veja o [ROADMAP.md](ROADMAP.md).

## Limitações

- Uma série de kernel testada (Debian 6.12.111; upstream 6.12.112 sob KASAN), x86-64, em VM;
  APIs do kernel mudam entre versões e o alvo fixado não é promessa de compatibilidade.
- Índice só em RAM, não paginável, limitado por `index_budget` (padrão 128 MiB), reconstruído
  por varredura linear a cada montagem; comportamento perto da saturação não testado.
- Montar exige `CAP_SYS_ADMIN` no namespace inicial; sem contêineres rootless.
- As UAPIs v1 e v2 podem mudar; a tag `v0.1.0-experimental` é um instantâneo do fonte, sem binários, empacotamento ou suporte.
- Só há números em nível de piloto (uma VM da Azure, uma semente de dados; registros 0025 a
  0031); nenhuma comparação com SQLite, FUSE ou o inferior puro pode ser afirmada como resultado
  antes do M4.
- Sem auditoria externa, syzkaller, xfstests completo ou documento de modelo de ameaça.
- A autorização do `QUERY` vale no instante da consulta (contrato TOCTOU); possíveis falsos
  negativos após renomear diretório, até remontar ([spec-m3](docs/spec-m3.md) §3.1).
- Atomicidade arquivo × metadados é convenção do cliente; durabilidade, reflink e dedup são do
  inferior. A aceitação rodou só sobre ext4.

## Comparado com arquivos comuns, SQLite e frameworks de memória

**Arquivos comuns.** Mesmo modelo de dados, sem módulo nem root, portabilidade total; a
consulta é `find` mais `getfattr`, varredura linear. Se consultas por atributo são raras,
arquivos ganham.

**SQLite ao lado dos arquivos.** SQL completo, índices compostos, décadas de maturidade, falhas
contidas no processo, transações atômicas dentro do banco; a aplicação faz a autorização e
mantém arquivos e linhas consistentes. Hoje é a recomendação fora de laboratório.

**Frameworks de memória (mem0, Letta, ai-memory).** Decidem o que lembrar e como montar o
prompt; o MoobFS guarda arquivos e responde predicados. São clientes potenciais sobre a
camada, não concorrentes; o plano do M4 os usa como cargas.

**MoobFS.** Um só armazenamento, autorização por resultado feita pelo SO, sem daemon, dados
legíveis sem o módulo; em troca, módulo de kernel de um autor, um kernel testado, índice só em
RAM e nenhuma vantagem medida. Tabela completa e condições em que pode valer avaliar:
[when-not-to-use.md](docs/when-not-to-use.md) (EN) e [value-assessment.md](docs/value-assessment.md).

## Experimentar em VM

Com Docker em execução, a partir da raiz do repositório:

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test
```

Compila as ferramentas e os dois módulos e inicia uma VM QEMU x86-64 com o kernel Debian
**6.12.111+deb13-amd64**. Não carrega módulos no host nem exige container privilegiado; nunca
carregue os módulos no seu host. No Apple Silicon usa emulação. Ambiente em
[docs/development.md](docs/development.md); o que cada marcador prova e não prova em
[docs/testing.md](docs/testing.md). Uma execução bem-sucedida termina com `MOOBFS_VM_PASS`,
`LAYER_LOWER_PASS` para ext4, btrfs e xfs, `DEMO_PASS`, `MOOBFS_ACCEPTANCE_PASS` e `MOOBFS_LAYER_PASS`
(texto exato no [README em inglês](README.md#try-it-in-a-vm)).

### Ferramentas de imagem do M1 (regressão)

As ferramentas precisam de compilador C11, Make e Python 3 no Linux ou macOS:

```sh
make test
printf 'Uma memória compartilhada.\n' > build/memory.txt
build/host/mkfs.moobfs build/demo.img alpha.txt=build/memory.txt beta.txt=build/memory.txt
build/host/fsck.moobfs build/demo.img
```

O relatório JSON mostra inodes distintos, mesmo objeto e mesmo offset. A criação recusa
sobrescrita; o verificador não repara imagens. A suíte da VM passa com 9 testes de
ferramentas, sanitizers e montagem real: inodes 2/3, offset físico 12288, extent de 16384
bytes, conteúdo lógico de 12562 bytes; testa leitura, EOF, seek, FIEMAP, remontagem, rejeição
de escrita e 15 imagens malformadas. Limites do M1: diretório plano, até 128 arquivos/objetos,
até 64 KiB por objeto, nomes ASCII imprimível de até 63 bytes; a imagem deve permanecer
imutável durante a montagem; compartilhar disco não garante cache comum. O módulo M1 não rodou
sob KASAN (o harness KASAN roda a suíte da camada) e não há benchmark.

## Mapa da documentação

Todos os documentos de `docs/`, com o idioma de cada um, estão no [índice da documentação](docs/README.md).

- [Visão da arquitetura](docs/architecture-overview.md), [comparação e quando não usar](docs/when-not-to-use.md) e [FAQ](docs/faq.md), em inglês.
- [Especificação M2](docs/spec-m2.md), [infraestrutura do kernel](docs/kernel-infrastructure.md), [ADR-0007](docs/decisions/0007-stacked-memory-layer.md); [especificação M3](docs/spec-m3.md); [especificação do `QUERY2`](docs/spec-query-v2.md); [plano](docs/benchmark-plan.md) e [harness](docs/benchmark-harness.md) do benchmark M4; [resultados de piloto](bench/results/README.md) (EN), com os pacotes brutos na release [`bench-evidence`](https://github.com/studiomoob/llmoobfs/releases/tag/bench-evidence).
- [Formato implementado do M1](docs/format-m1.md) e [proposta original do MVP](docs/native-mvp.md) (interface de escrita substituída pela ADR-0007).
- [Decisões](docs/decisions/), [histórico](docs/history/), [levantamento do ecossistema](docs/ecosystem.md), [anterioridade](docs/research-originality.md) e [changelog](CHANGELOG.md).
- [Regras para Codex, Claude e Cursor](docs/ai-workflow.md).
- [Contribuição](CONTRIBUTING.md), [governança](GOVERNANCE.md), [conduta](CODE_OF_CONDUCT.md) e [segurança](SECURITY.md).

## Anterioridade e posicionamento

O MoobFS se apoia em precedentes que reconhece: o Semantic File System (1991) e o Be File
System (1997) para consultas por atributos, overlayfs e ecryptfs para o modelo empilhado, e o
YoloFS (2026), que já demonstra um módulo de kernel empilhado voltado a agentes. O `QUERY2` por
domínio segue a pesquisa sobre busca com controle de acesso: particionar o índice pelo conjunto de
leitores (Singh et al. 2007; GUFI, SC22) fecha os vazamentos que filtrar depois da busca deixa
abertos (Büttcher e Clarke, FAST 2005; Wang et al., S&P 2017). O projeto não reivindica nenhuma
dessas ideias como inédita; o que explora é a combinação delas numa camada empilhada no kernel, e
se isso tem valor mensurável ([research-originality.md](docs/research-originality.md) §4, §5, §9).
Cada obra, projeto e ferramenta usada está listada com fontes conferidas em
[references.md](docs/references.md).

## Contribuição e segurança

Relatos reproduzíveis, revisão de formato e de kernel e melhorias de reprodutibilidade são
bem-vindos. Leia [CONTRIBUTING.md](CONTRIBUTING.md) (DCO, requisitos de teste, regras de
revisão). Perguntas vão para o GitHub Discussions ou uma issue. Não coloque detalhes privados
de vulnerabilidade em issues públicas; use o [contato de segurança](SECURITY.md).

## Licença

Todo o projeto usa **GPL-2.0-only**, conforme [LICENSE](LICENSE).
