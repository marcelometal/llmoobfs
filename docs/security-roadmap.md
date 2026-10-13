# Roadmap de segurança e robustez da camada

Estado: **proposta** (2026-10-10). Nenhum item abaixo marcado como "a fazer" foi implementado.
Origem: levantamento registrado em [docs/history/0017](history/0017-macos-and-security-posture.md),
que compara o MoobFS com a documentação do kernel para filesystems novos, com a literatura de
fuzzing de filesystems (Janus, Hydra, LFUZZ) e com o histórico de CVEs de overlayfs, eCryptfs,
ntfs3, JFS, ext4 e OpenZFS. Este documento é a lista de técnicas a implementar, com ordem
sugerida e critério de pronto. Decisões de desenho vão para ADRs; evidências de execução vão
para `docs/history/`.

Princípio: um módulo roda no anel 0, logo um bug nele é um bug no kernel inteiro. Nenhuma
técnica elimina isso. O objetivo é reduzir probabilidade e impacto com entrada validada na
borda, detectores em CI, fuzzing contínuo, superfície mínima e modelo de ameaça escrito. Um
crash disparável por usuário comum é vulnerabilidade (negação de serviço), mesmo sem exploit,
como os maintainers de XFS e ext4 tratam.

## Estado atual (verificado em 2026-10-10)

| Técnica | Onde | Estado |
| --- | --- | --- |
| Validação de entrada de ioctl (`QUERY`, `GET_INFO`): limite, flags, reservados, `copy_to_user` com tamanho fixo | `layer/index.c` | Implementado |
| Validação de atributos tipados antes de tocar o inferior (tipo, tamanho, alfabeto do nome, UTF-8, `EPERM` em `user.moob.format`) | `layer/attr.c`, `common/attr.c` | Implementado, testado na VM |
| Permissão em dupla camada (`inode_permission` no inferior mais a verificação do próprio inferior) e `override_creds` do montador | `layer/main.c` | Implementado, modelo não escrito |
| KASAN, UBSAN e lockdep em kernel próprio; falha em `BUG`, `Oops`, `WARNING`, `KASAN`, `UBSAN`, lockdep no `dmesg` | `vm/kasan-guest.sh`, `.github/workflows/kasan.yml` | Implementado, sob demanda |
| ASan e UBSan no código compartilhado de `common/` | `.github/workflows/ci.yml` | Implementado, a cada push |
| libFuzzer sobre a codificação de atributos | `tests/fuzz_attr.c` | Implementado, 30 s por push |
| Orçamento de memória do índice (`index_budget`, `index=off`) | `layer/index.c` | Implementado |
| Módulo carregado só em VM QEMU descartável | `vm/` | Implementado |
| `fsx` do xfstests, `dm-log-writes`, `dm-flakey` | `vm/suite.sh` | Implementado |
| `-Wall -Wextra -Werror -Wconversion -Wshadow` nas ferramentas | `Makefile` | Implementado |

## Técnicas a implementar

Ordem sugerida por custo-benefício. Cada item tem critério de pronto verificável.

### S1. Modelo de ameaça e de credenciais (ADR)

> Nota de 2026-10-11 (registro 0030), medida no host da Azure: o tempo do `QUERY` cresce cerca
> de 1,15 µs por entrada de outro dono que casa com a consulta (18 µs com nenhuma, 28 ms com
> 20 000), com variação abaixo de 3%. Um agente consegue estimar quantas memórias alheias têm um
> valor de atributo e testar se um valor existe em qualquer lugar. A ADR de S1 precisa decidir o
> modelo; a mitigação estrutural é não percorrer entradas de outros domínios (ADR-0009).

> Nota de 2026-10-11 (registro 0028): a autorização por resultado do `QUERY` cobre DAC, ACLs e
> módulos de segurança baseados em inode, mas não consulta os que decidem pelo caminho na
> abertura (AppArmor, Landlock). Um processo confinado por eles pode ver nos resultados um
> arquivo que não conseguiria abrir. A ADR de S1 precisa decidir se isso entra no contrato.

Precedente: a discussão de `override_creds=off` no overlayfs mostrou que, sem um modelo
escrito, revisores não conseguem dizer se o esquema de credenciais é explorável.

- Escrever ADR com: quem é o adversário (usuário local sem privilégio com acesso ao ponto de
  montagem; processo que grava no inferior por fora; conteúdo do inferior em disco), o que a
  camada promete (não derrubar o kernel, não escalar privilégio, não expor dados que o inferior
  não exporia) e o que não promete (confidencialidade além do inferior, integridade contra
  modificação do inferior enquanto montado).
- Declarar explicitamente que `FS_USERNS_MOUNT` fica desligado e que montagem exige
  `CAP_SYS_ADMIN` no namespace inicial. Precedente: CVE-2015-1328, CVE-2016-1576,
  CVE-2020-16120 e CVE-2026-74619 no overlayfs, todos pela flag ligada; distribuições que a
  deixam desligada evitam a classe inteira.
- Listar as superfícies não privilegiadas: `setxattr`/`removexattr` em `user.moob.*`,
  `QUERY`, `GET_INFO`, `FICLONE*`, `FIDEDUPERANGE`, `FIEMAP`, `open_by_handle_at` com handles
  devolvidos pelo `QUERY`.
- Pronto quando: ADR publicada, `fs_flags` do módulo revisado contra ela, e `moobfs-check`
  reporta a versão do modelo.

### S2. fstests (xfstests) sobre a camada

*Status, 2026-10-13 (journal 0075; in English).* Started: fstests runs over the layer with a small
port (`vm/fstests/`), and the `quick` group has one recorded run over ext4. btrfs and XFS, the
exclusion list and the four gaps it showed are open.

*Status, 2026-10-13 (journal 0077; in English).* The `quick` group is recorded over btrfs and XFS
too; the same 15 tests fail on the three lowers. Open: the exclusion list, the four gaps, the
`auto` group.

Precedente: a documentação do kernel "Adding New Filesystems" chama fstests de infraestrutura
padrão e seu uso de fortemente recomendado; bcachefs reportou fstests sob lockdep e KASAN antes
do merge.

- Adicionar configuração de fstests para `moobfs_layer` com inferior ext4, btrfs e XFS, na
  linha do que o overlayfs faz (`FSTYP=overlay` com `OVL_BASE_*`).
- Rodar o grupo `quick` na VM; registrar pass, fail e not-run por inferior.
- Pronto quando: lista de exclusões justificada por teste e resultado reproduzível em
  `make vm-test` ou alvo próprio.

### S3. syzkaller sobre os ioctls e o setxattr

Precedente: Janus achou 90 bugs em oito filesystems do kernel com 32 CVEs; Hydra achou mais
155 fuzzando imagem e syscalls em conjunto. O libFuzzer atual cobre só o parser da codificação.

- Escrever descrições syzlang (`sys/linux/moobfs.txt` num checkout local do syzkaller) para
  `openat$moobfs_root`, `ioctl$MOOB_QUERY`, `ioctl$MOOB_GET_INFO`, `setxattr$moob`,
  `removexattr$moob`, com structs tipadas e flags; constantes via `.const` ou `syz-extract`.
- Rodar `syz-manager` contra a VM com o kernel KASAN e lockdep já existente; coletar cobertura
  de `layer/`.
- Pronto quando: cobertura dos handlers de ioctl e xattr acima de um limiar definido, zero
  relatos abertos, e reprodutores de crashes anteriores guardados em `tests/`.

### S4. Inferior como entrada hostil

*Status, 2026-10-13 (journal 0073; in English, as the rest of the repository since ADR-0010).*
Done for the scope below: `tests/hostile.c` generates the adversarial trees, `vm/suite.sh` mounts
the layer over them on ext4, btrfs and XFS (`HOSTILE_LOWER_PASS`, also under KASAN), and
`make hostile-test` runs them through the tools under sanitizers in CI. The behaviour is recorded
in ADR-0011. The choice made: an invalid attribute is skipped silently and reported by
`moobfs-check`, not logged in `dmesg`, because a writer on the lower could otherwise fill the log.

Precedente: CVE-2019-19319 (ext4, `setxattr` após imagem forjada escreve fora de limites),
CVE-2009-2406 (eCryptfs, overflow em parser de metadados), posição dos maintainers de que o
kernel não promete segurança ao montar imagem hostil.

- Tratar cada xattr lido na varredura de montagem e em `getxattr` como não confiável: a camada
  não pode confiar que `user.moob.*` no inferior passou pela sua própria validação, porque pode
  ter sido gravado por fora.
- Alvo de fuzz que gere diretórios inferiores adversariais (nomes, tamanhos, tipos, payloads
  truncados, contagem acima de 64, `user.moob.format` inválido) e monte a camada sobre eles na
  VM; versão em espaço de usuário com `moobfs-index` sob ASan como primeira linha.
- Definir comportamento: atributo inválido no inferior é ignorado no índice e reportado em
  `dmesg` com rate limit, ou faz a montagem falhar; registrar a escolha na ADR de S1.
- Pronto quando: fuzz sem crash por tempo definido e comportamento documentado e testado.

### S5. Lint e análise estática de kernel no CI

- `checkpatch.pl`, `sparse` (`make C=1`) e `smatch` sobre `layer/`; `W=1` no build do módulo.
- `scripts/kernel-doc` para a UAPI em `include/moobfs_ioctl.h`.
- Pronto quando: zero avisos ou lista de supressões justificada.
- Estado (2026-10-12, registro 0050): `checkpatch.pl` com zero avisos em `layer/` e `kernel/`, sem
  supressões, e obrigatório no job `Lint`. `sparse`, `smatch`, `W=1` e `kernel-doc` continuam
  pendentes.

### S6. Processo de divulgação e política de versões

*Status, 2026-10-13 (journal 0080; in English).* `SECURITY.md` is published. The policy of
supported kernels and of compatibility is in `docs/compatibility.md`, linked from the README,
accepted by the maintainer the same day. S6's "done when" is met; the build-time check of an
unlisted kernel is a follow-up.

- `SECURITY.md` com canal de contato, prazo de resposta e o que conta como vulnerabilidade
  (inclui DoS por usuário comum).
- Política de kernels suportados e de compatibilidade da UAPI, já prevista no M1 hardening.
- Pronto quando: arquivo publicado e linkado no README.

### S7. Fuzzing contínuo e maior

- Elevar `FUZZ_TIME` do libFuzzer em workflow agendado; guardar corpus em artefato.
- Avaliar Janus/Hydra ou LFUZZ para fuzzing de imagem mais syscalls sobre o inferior montado
  pela camada; registrar viabilidade.
- Pronto quando: workflow agendado existe e um relatório por rodada vai para `docs/history/`.

### S8. Hardening em tempo de execução

- Ativar e testar com `CONFIG_FORTIFY_SOURCE`, `CONFIG_SLAB_FREELIST_HARDENED`,
  `CONFIG_INIT_ON_ALLOC_DEFAULT_ON`, KFENCE e KCSAN no kernel de teste; corrigir o que aparecer.
- `WARN_ON_ONCE` em vez de `WARN_ON` em caminhos disparáveis por usuário (precedente:
  CVE-2026-74619, overlayfs, `WARN_ON` em loop derruba kernel com `panic_on_warn`).
- Rate limit em toda mensagem de `dmesg` disparável por usuário (`pr_warn_ratelimited`).
- Pronto quando: suíte passa com as opções ligadas e revisão de `WARN_ON`/`pr_*` concluída.

### S9. Limites de recursos e negação de serviço

- Teto por montagem e por usuário para resultados de `QUERY` e para memória do índice,
  já parcialmente coberto por `index_budget`; testar comportamento ao atingir o teto.
- Custo da varredura de montagem com inferior grande; `index=off` e opção de índice preguiçoso
  como válvula.
- Pronto quando: testes de limite na suíte e números registrados.

### S10. Assinatura e distribuição do módulo

- Assinatura do módulo e compatibilidade com Secure Boot (lockdown); empacotamento DKMS.
- Reproducible build do `.ko` com hash registrado.
- Pronto quando: módulo assinado carrega em VM com lockdown ativo.

### S11. Superfícies de M3 e M4

Para registrar quando chegarem: handles de `open_by_handle_at` devolvidos pelo `QUERY`
(precisam de `CAP_DAC_READ_SEARCH` no chamador, verificar que a camada não contorna isso);
revisões e snapshots (vazamento de dados entre revisões; garbage collection como superfície de
DoS); clientes LLM (políticas de conteúdo ficam no cliente, não na camada).

## Fora de escopo desta lista

- Guardrails de conteúdo para LLM: a camada é armazenamento; controle de o que um agente lê ou
  grava fica nas permissões POSIX do inferior, em `nolinks` e no cliente de memória.
- Criptografia: delegada ao inferior (fscrypt, dm-crypt), conforme ADR-0007.
- Suporte a user namespaces: fora do M2; se entrar, é uma ADR nova com os CVEs do overlayfs
  como lista de verificação.

## Fontes

Listadas com links no registro [0017](history/0017-macos-and-security-posture.md): documentação
do kernel "Adding New Filesystems"; Janus (IEEE S&P 2019), Hydra (SOSP 2019), LFUZZ (2024);
discussões em linux-fsdevel e LWN sobre imagens hostis (2018, 2023, 2025); CVEs do overlayfs
(2015-1328, 2016-1576, 2020-16120, 2026-74619), eCryptfs (2009-2406, 2014-9922), ntfs3
(2026-63833), JFS (2010-2946), ext4 (2019-19319), setxattr (2016-7097); OpenZFS `ztest` e
correção de userns de 2026; documentação de descrições do syzkaller.
