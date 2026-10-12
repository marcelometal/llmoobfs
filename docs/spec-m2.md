# Especificação do M2 — camada de memória empilhada sobre filesystem maduro

Estado: **proposta** (2026-10-10). Nada implementado ou testado. Decisões em
[ADR-0005](decisions/0005-memory-model-bfs-reference.md) e
[ADR-0007](decisions/0007-stacked-memory-layer.md). A versão anterior desta especificação
(volume completo, ADR-0006) está preservada em
[spec-m2-full-volume-superseded.md](spec-m2-full-volume-superseded.md).

## 1. O que é

`moobfs` é um módulo do kernel que monta uma **visão de memória** sobre um diretório de um
filesystem maduro:

```sh
mount -t moobfs -o lower=/srv/memory/store,index_budget=128M none /mnt/memory
```

Tudo que está em `/mnt/memory` é um arquivo ou diretório real em `/srv/memory/store`. A camada
acrescenta: validação de atributos tipados, índice de atributos em memória, consulta por
atributo (`QUERY`), capacidades do inferior expostas de forma uniforme e, nas fases seguintes,
identidade e proveniência de memórias.

Princípios:

- **Nada preso.** Desmontado o MoobFS, o diretório inferior é um conjunto comum de arquivos
  com xattr; `grep`, `getfattr`, `git`, `rsync` e `duperemove` funcionam nele sem a camada.
- **Nada reinventado.** Blocos, journal, diretórios, mmap, locks, reflink, checksums e
  snapshots são do inferior. Git versiona conteúdo. A camada não tem formato em disco próprio
  além da codificação dos valores de xattr.
- **Degradação explícita.** O que o inferior não oferece é reportado, não simulado.

## 2. Escopo do M2

Entra: passagem completa de operações POSIX para o inferior (arquivos, diretórios, rename,
unlink, truncate, mmap, fsync, locks, splice); atributos tipados em `user.moob.*` validados na
escrita; índice em RAM e `QUERY` por ioctl; reflink e dedup delegados; `GET_INFO` com
capacidades; ferramentas `moobfs-check` e `moobfs-index`; testes sobre btrfs, XFS e ext4.

Fica fora: revisões e identidade de memória (M3), consulta temporal (M3), índice persistente,
dedup automático na escrita, qualquer alteração do inferior fora do diretório montado, suporte a
mais de um inferior, exportação NFS, user namespaces não triviais (segue o que o overlayfs
permite, avaliado caso a caso).

## 3. Modelo de dados

| Conceito | Representação no inferior | Papel da camada |
| --- | --- | --- |
| Memória | Arquivo comum | Expõe como arquivo; valida atributos |
| Coleção | Diretório comum | Passagem |
| Atributo tipado | xattr `user.moob.<nome>` = `tipo (1 byte) + payload` | Valida tipo, tamanho e nome; indexa |
| Atributos opacos | Qualquer outro xattr | Passagem sem validação ou índice |
| Versão da codificação | xattr `user.moob.format` no diretório raiz inferior, valor `1` | Recusa montar versão maior; migra versão menor só com ferramenta |
| Conteúdo compartilhado | Reflink do inferior (btrfs, XFS) | Delegado; `FIEMAP` passa a flag `SHARED` do inferior |

Tipos: `string` (UTF-8 válido, até 1 KiB), `int64`, `double`, `bool`, `time` (ns desde epoch,
int64), `raw` (até 4 KiB, não indexado). Nome: `[a-z0-9_.]`, 1 a 64 bytes. Até 64 atributos
tipados por arquivo. Limites são tetos de experimento, configuráveis em compilação.
`setxattr` com tipo inválido, tamanho fora do limite ou nome fora do alfabeto retorna `EINVAL`;
`user.moob.format` é reservado e retorna `EPERM` pela camada. Criar o 65.º atributo tipado
retorna `ENOSPC`. O inferior pode impor um limite menor pelo tamanho do seu bloco de xattr
(ext4 com blocos de 1 KiB comporta cerca de 36 entradas pequenas; com 4 KiB, mais de 64), e
esse `ENOSPC` propaga sem ser mascarado: o limite efetivo é o menor dos dois.

A camada não impõe esquema. Convenções de nomes (por exemplo `kind`, `tier`, `agent`,
`session`, `created`, `expires`) ficam em documento de recomendação para clientes, não no kernel.

## 4. Operações

| Operação | Comportamento |
| --- | --- |
| lookup, readdir, create, mkdir, unlink, rmdir, rename, truncate, setattr | Passagem para o inferior com as credenciais de quem montou (`override_creds`), como no overlayfs; permissões verificadas na camada **e** no inferior |
| link, symlink | Passagem se o inferior suportar; opção de montagem `nolinks` para recusar (`EPERM`), recomendada para clientes como o ai-memory, cujo confinamento rejeita symlinks |
| read, write, mmap, splice, fsync, fallocate, locks | Delegados por `backing_file_*`; `fsync` chama o `fsync` do inferior; semântica de durabilidade é a do inferior |
| `O_DIRECT` | Passagem; se o inferior recusar, propaga o erro |
| getxattr, listxattr, removexattr | Passagem |
| setxattr em `user.moob.*` | Valida tipo, tamanho, nome e UTF-8; grava no inferior; atualiza o índice sob o lock do inode; erro de validação retorna `EINVAL` sem tocar o inferior |
| setxattr em outros namespaces | Passagem |
| `FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE` | `vfs_clone_file_range` / `vfs_dedupe_file_range_one` nos arquivos inferiores; `EOPNOTSUPP` se o inferior não suportar; o VFS já comparou bytes no dedup |
| `FIEMAP` | Passagem |
| `QUERY` (ioctl no diretório raiz montado) | Predicado de até 4 termos `(atributo, operador ∈ {=, <, <=, >, >=, prefixo}, valor tipado)` em conjunção, limite de resultados; retorna `(número de inode da camada, handle de arquivo)` para `open_by_handle_at`, mais caminho relativo quando disponível por lookup reverso limitado |
| `GET_INFO` (ioctl) | Versão da UAPI, versão da codificação, limites, orçamento e uso do índice, capacidades do inferior (`reflink`, `dedup`, `data_checksums`, `snapshots`, `xattr`), tipo do inferior |

Montagem `ro`: nenhuma escrita chega ao inferior; o índice é construído normalmente.

## 5. Índice de atributos

- Construído na montagem por varredura do diretório inferior (percurso com `iterate_dir` e
  `vfs_getxattr` nos arquivos). Custo proporcional ao número de arquivos; orçamento de memória
  `index_budget` (padrão 128 MiB); ultrapassado o orçamento, a montagem falha com `ENOMEM` e
  mensagem clara, ou monta sem índice com `index=off` (`QUERY` retorna `ENOSYS`).
- Estruturas: por atributo, `xarray` para inteiros/tempos/booleanos e `rbtree` para strings e
  doubles, chaveadas pelo valor codificado; cada entrada aponta para o inode da camada.
- Atualizado em `setxattr`, `removexattr`, `unlink`, `rename` (apenas lookup reverso) e
  `create`, dentro do mesmo caminho de código, sob o lock do inode. Não persistido: a próxima
  montagem reconstrói. Isso também absorve modificações feitas no inferior por fora enquanto
  desmontado.
- Modificar o inferior **enquanto montado** é indefinido, como no overlayfs; `moobfs-check`
  detecta divergência entre índice e inferior por varredura comparativa.

> Nota (2026-10-12, registro 0050): com o índice desligado, `QUERY` passou a devolver
> `EOPNOTSUPP` em vez de `ENOSYS`; `ENOSYS` fica reservado a chamada de sistema inexistente.

## 6. Consistência e durabilidade

- Tudo que o inferior garante, a camada herda; nada além disso. `fsync` bem-sucedido significa
  o que significa no inferior.
- Conteúdo e atributos não são gravados atomicamente juntos (são operações distintas no
  inferior). Convenção recomendada aos clientes: gravar conteúdo em arquivo temporário, definir
  atributos, `fsync` do arquivo, `rename`, e `fsync` do diretório pai. Com essa ordem, um
  arquivo visível no nome final tem conteúdo e atributos completos, e o nome sobrevive a uma
  queda de energia. Sem o `fsync` do diretório, o `rename` só se torna durável no próximo commit
  do journal do inferior (verificado com `dm-log-writes` na fase 2e: o arquivo renomeado não
  estava presente na marca gravada logo após o `rename`).
- Falha de I/O no inferior propaga ao chamador; a camada não marca estado próprio.
- Queda de energia: o inferior recupera; a camada reconstrói o índice.

## 7. Ferramentas (espaço de usuário)

- `moobfs-check <lower>`: valida codificação de todos os `user.moob.*`, versão do formato,
  limites; saída JSON; não repara.
- `moobfs-index <lower>`: constrói o índice em espaço de usuário com o mesmo código de
  `common/` e responde consultas; serve de oráculo para testar o `QUERY` do kernel e como
  utilitário para quem não quer montar.
- `moobfs-attr`: `get`/`set`/`list` tipados por cima de `getfattr`/`setfattr`, para scripts.
- Dedup: `duperemove` ou `cp --reflink` do próprio sistema; nenhuma ferramenta própria.
- Importação de imagens M1: `moobfs-import-m1 imagem.img <lower>` extrai arquivos e, se o
  inferior suportar, aplica `FIDEDUPERANGE` para reproduzir o compartilhamento.

## 8. Entradas inválidas que devem ser rejeitadas

Na montagem: inferior inexistente, não é diretório, é o próprio ponto de montagem ou contém
`user.moob.format` com versão maior; inferior sem suporte a xattr (`user.`) monta só com
`index=off` e aviso. Em `setxattr`: tipo desconhecido, payload de tamanho errado para o tipo,
UTF-8 inválido em `string` (inclui NUL, sequências longas demais, substitutos e pontos de
código acima de U+10FFFF), `double` NaN, `bool` fora de 0/1, nome fora do alfabeto, nome
reservado (`EPERM`), excesso de atributos (`ENOSPC`). Na varredura: xattr
`user.moob.*` malformado em arquivo existente é registrado (`moobfs-check` lista) e o arquivo
fica fora do índice; a montagem não falha. Em `QUERY`: operador inválido, tipo do valor
incompatível com o atributo, mais de 4 termos, limite de resultados fora da faixa, estrutura com
campos reservados não zerados. Cada caso tem teste nas ferramentas e no módulo.

## 9. Testes e evidência exigida

1. `make test`: `common/` (codificação, predicados, índice em espaço de usuário), ferramentas,
   sanitizers, fuzzing da codificação e do parser de `QUERY`.
2. `make vm-test`, com evidência em `build/vm/`:
   - xfstests grupo `generic` sobre o mount moobfs com inferior btrfs, XFS e ext4, seguindo o
     modelo de `./check --fs overlay`; lista de exclusões versionada e justificada;
   - `fsx` e `fsstress` sobre o mount;
   - `dm-log-writes` sob o inferior, com carga de `write`/`setxattr`/`fsync`/`rename`, replay
     em cada marca e verificação de que todo arquivo no nome final tem atributos completos;
   - `dm-flakey` no inferior: erros propagam, índice coerente após remontagem;
   - entradas inválidas da seção 8;
   - `QUERY` comparado com `moobfs-index` (mesmas consultas, mesmos resultados) e, para as
     semânticas de operador, com `bfs_shell` do Haiku;
   - reflink e dedup com `cp --reflink` e `duperemove` sobre btrfs e XFS; `EOPNOTSUPP` sobre ext4;
   - **aceitação com clientes reais que não conhecem o MoobFS**: `git init`, commits e
     `git fsck` no mount; SQLite em WAL com escritor e leitor concorrentes e `PRAGMA
     integrity_check`; e um cliente de memória próprio (script que grava páginas markdown por
     tmp + `rename` + `fsync`, define atributos tipados e consulta por `QUERY`). Sucesso é
     conteúdo íntegro e `moobfs-check` limpo após desmontar. Nenhum projeto externo de memória
     de agentes faz parte do critério: eles entram no M4 como sistemas a comparar.
3. KASAN e `PROVE_LOCKING` via virtme-ng antes de qualquer marco declarado.
4. O M1 continua executado em `make vm-test` como regressão do experimento concluído.

## 10. Compatibilidade

A camada não tem formato em disco além da codificação de xattr, versionada por
`user.moob.format`. Mudanças incompatíveis na codificação exigem versão nova, ferramenta de
migração offline e decisão explícita. A UAPI dos ioctls tem versão própria; comando desconhecido
retorna `ENOTTY`. Imagens M1 continuam legíveis pelo módulo M1 preservado no histórico e
importáveis pela ferramenta da seção 7.

## 11. Fases e critério de saída

| Fase | Entrega | Saída |
| --- | --- | --- |
| 2a | Passagem mínima (wrapfs): lookup, readdir, create, read/write via `backing_file_*`, mmap, fsync, sobre ext4 | **Validada em 2026-10-10** ([registro 0011](history/0011-phase-2a-stacked-passthrough.md)): `fsx` passa e teste de passagem passa na VM; xfstests `generic` e KASAN ficam para 2b |
| 2b | Passagem completa (rename, unlink, truncate, locks, splice, xattr opacos); btrfs e XFS como inferiores; reflink delegado | **Validada em 2026-10-10** ([registro 0012](history/0012-phase-2b-full-passthrough.md)): `FICLONE`/`FIDEDUPERANGE` funcionam sobre btrfs e XFS; xfstests completo e KASAN/lockdep ainda pendentes |
| 2c | Codificação tipada, validação, `moobfs-check`, `moobfs-attr`, fuzzing | **Validada em 2026-10-10** ([registro 0013](history/0013-phase-2c-typed-attributes.md)): entradas inválidas rejeitadas no kernel e pelas ferramentas; fuzzing no CI |
| 2d | Índice em RAM, `QUERY`, `GET_INFO`, `moobfs-index` como oráculo | **Validada em 2026-10-10** ([registro 0014](history/0014-phase-2d-index-query.md)): dez consultas idênticas kernel/oráculo em ext4, btrfs e XFS; `noindex` e `index_budget` exercitados |
| 2e | Aceitação com git + SQLite + cliente de memória próprio; dm-log-writes; dm-flakey | **Validada em 2026-10-10** ([registro 0015](history/0015-phase-2e-investigation.md)): dois defeitos da camada encontrados e corrigidos; xfstests completo e KASAN/lockdep seguem pendentes |

## 12. Questões em aberto

- Lookup reverso para devolver caminhos no `QUERY`: manter um mapa inode→dentry na camada ou
  devolver só handles (proposta: handles sempre; caminhos como extensão opcional).
- Comportamento com inferior montado por outro usuário ou com user namespaces (seguir o que o
  overlayfs permite em 6.12 e documentar).
- Se `user.moob.*` deve migrar para `trusted.moob.*` quando o mount for de root, para evitar que
  processos sem privilégio alterem atributos por fora da camada.
- Convenções de nomes de atributos para clientes: documento de recomendação a escrever junto
  com a fase 2c, usando o front matter do ai-memory como primeiro caso.
