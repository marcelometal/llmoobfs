# Avaliação crítica de utilidade e proposta de valor técnica do MoobFS

Estado: análise (2026-10-10, branch `m3/3b-query-authz`). Não altera ADRs, formato, UAPI nem
código. Base: README, ROADMAP, ADR-0005/0006/0007, `docs/spec-m2.md`, `docs/spec-m3.md`,
`docs/benchmark-plan.md`, `docs/benchmark-harness.md`, `docs/security-roadmap.md`,
`docs/research-originality.md` (§4, §5, §7), `docs/distribution-strategy.md`, registros
0014, 0015, 0016, 0020, 0021, `layer/index.c`, `layer/main.c`, `include/moobfs_ioctl.h`.

Rótulos usados em cada afirmação:

- **comprovado**: há teste ou leitura de código no repositório que sustenta (com a ressalva do
  ambiente: um autor, VM, kernel Debian 6.12.111 e, sob demanda, 6.12.112 com KASAN).
- **potencial**: decorre plausivelmente do mecanismo, mas nenhuma medição existe.
- **não demonstrado**: sem evidência, ou com evidência que não cobre a afirmação.

Fato central, que condiciona todo o texto: **nenhum número de desempenho foi medido**
(`docs/benchmark-plan.md`, abertura). Toda afirmação de ganho de velocidade, memória ou custo é,
no máximo, potencial. Texto escrito com esse limite; "estimativa, não medida" marca derivações.

---

## 1. Rapidez e desempenho

### 1.1 Princípio de comparação

Só se comparam operações semanticamente equivalentes. "Consulta por atributo" no MoobFS é uma
conjunção de até 4 predicados tipados (eq/lt/le/gt/ge/prefix/exists) sobre `user.moob.*`, com
autorização por resultado. A operação equivalente nos baselines é uma consulta SQL com índice
composto sobre uma tabela mantida pela aplicação (baselines A e B do plano), e, para "arquivos
puros", `find` + `getfattr` (varredura linear). Busca semântica, OR e texto livre não existem
no MoobFS e **não entram** em nenhuma comparação.

Custo estrutural certo, independente de medição: a camada insere um salto de VFS em toda
operação (dentry/inode superior que delega ao inferior; I/O via `fs/backing-file.c`;
`override_creds` com as credenciais de quem montou em operações delegadas). Esse custo é
**certo em sinal** (positivo) e **não medido em magnitude**. Nenhuma operação do MoobFS pode
ser mais rápida que a mesma operação direta no inferior, exceto onde substitui trabalho que o
baseline faria em duas etapas (arquivo + banco).

### 1.2 Por operação

| Operação | Onde a camada pode ganhar | Onde certamente acrescenta custo | Métricas que mostrariam ganho relevante | Carga |
| --- | --- | --- | --- | --- |
| Criar/atualizar memória (tmp + `setxattr` + fsync + rename + fsync dir) | **Potencial**: um store só, sem segunda escrita (SQLite exige gravar arquivo e depois a linha; duas durabilidades). Atualizar o índice é operação em RAM sob lock | **Certo**: salto de VFS por chamada; validação de tipo no `setxattr`; `index_put` sob `down_write` do rwsem global do índice (serializa escritores de atributos de todos os inodes); `record_dentry_path` aloca uma página e monta o caminho a cada `setxattr`. Durabilidade é a do inferior, sem ganho | p50/p95/p99 de W1 por chamada; write amplification (`WA_relative` do plano: baseline inclui WAL + páginas + índices SQLite); CPU por criação | W1, W4 |
| Ler por caminho conhecido | Nenhum ganho plausível: o dado é lido do inferior via `backing_file_read_iter` | **Certo**: o salto extra. Magnitude desconhecida; para arquivos pequenos em cache de página tende a ser proporcionalmente visível, mas isto é hipótese | W2: p50/p99 de `open+read+close` direto versus pela camada, mesmos bytes, cache quente e frio. Ganho esperado: nenhum; critério é "overhead aceitável" | W2 |
| Consulta de 1 atributo | **Potencial**: `QUERY` percorre a rbtree do atributo no kernel, sem troca de contexto de processo nem IPC com banco; uma chamada `ioctl` | **Certo**: autorização por resultado (`vfs_path_lookup` + `inode_permission` por candidato, lotes de 256) que o SQL não faz; `kstrdup(GFP_ATOMIC)` por candidato sob o lock de leitura; varredura é linear nas entradas do primeiro termo, **sem uso de intervalo na rbtree** (registro 0014, "Limitações") | W3 (Q1, Q3, Q6, Q7, Q9): p50/p95/p99 e CPU por consulta, para seletividades de 0,1% a 50%, com razão de candidatos privados 1:1 a 100:1 | W3 |
| Consulta multi-filtro (2 a 4 termos) | **Potencial** limitado: um índice por atributo (não composto): escolhe-se um termo (o primeiro, não o mais seletivo na implementação lida) e filtra pelos demais pela lista do inode | **Provável desvantagem frente a SQLite**: o SQLite usa índice composto com igualdade antes de intervalo e o otimizador escolhe o plano; a camada varre todas as entradas do primeiro termo. Se o primeiro termo é pouco seletivo, o custo é O(N) em RAM mais autorização de cada candidato que passa nos demais termos | W3 (Q2, Q4, Q5, Q8): p99 versus SQLite com índice composto, com gate de `EXPLAIN QUERY PLAN` obrigatório | W3 |
| Recuperação entre sessões (remount + primeira consulta) | **Potencial**: não há banco para abrir/recuperar; mas o índice **não é persistente**, então a primeira consulta só responde depois da varredura completa | **Certo**: tempo de montagem proporcional ao número de arquivos (percurso + `listxattr` + `getxattr` por atributo). SQLite abre um arquivo e responde em milissegundos independentemente do número de linhas | W6 (frio), W7 (quente): tempo até o primeiro `QUERY` correto, separando I/O de CPU, em S1..S6. Se crescer linear até minutos, o argumento enfraquece | W6, W7 |
| Multiagente concorrente | **Potencial**: sem processo de banco intermediando; leitura de arquivos é concorrente como no inferior; `QUERY` usa lock de leitura | **Certo**: rwsem único por montagem; `setxattr` de qualquer agente toma o lock de escrita; consulta larga segura o lock de leitura por lote, atrasando escritores. SQLite WAL tem um escritor por vez, mas leitores não bloqueiam | W8 (1, 10, 100 processos): vazão e p99 de escrita sob consultas; W5 (rename/remoção concorrentes) | W5, W8 |
| Montagem / reconstrução do índice | Nenhum | **Certo**: custo linear, em RAM, sem persistência (decisão: nenhuma persistência antes do M4) | W6/W7 em S1..S6 e saturação 50..100% do orçamento | W6, W7 |

### 1.3 Resumo

Comprovado: correção contra oráculo (árvore de 6 inodes) e execução sem modificação de git e SQLite WAL (200 inserções). Não demonstrado: qualquer ganho de latência, vazão, CPU ou espaço. Para W1, a comparação só é equivalente se a durabilidade for igualada (SQLite em WAL com `synchronous=NORMAL` pode perder as últimas transações em queda; o MoobFS exige `fsync` explícito).

## 2. Eficiência de memória

### 2.1 Estruturas reais (`layer/moobl.h`, `layer/index.c`)

- `moobl_ix_name`: um por nome de atributo distinto. `rb_node` (24 B) + `rb_root` (8 B) +
  `count` (4 B) + `name[65]`: cerca de 104 B por nome. Desprezível (dezenas de nomes).
- `moobl_ix_inode`: um por inode com pelo menos um atributo indexável. `ino` (8) + `list_head`
  (16) + `upper` (8) + `path` (8) = 40 B contabilizados; o alocador de slabs entrega uma
  classe de 64 B. Mais a entrada do `xarray` (ver abaixo).
- `moobl_ix_entry`: uma por (inode, atributo indexado). `rb_node` (24) + `list_head` (16) +
  dois ponteiros (16) + `type` e `len` (arredondados a 8) = **64 B de cabeçalho** + payload
  (`int64`/`time`/`double` 8 B; `bool` 1 B; `string` até 1024 B). `raw` não é indexado.
- Caminho: `kstrdup`/`kmemdup` do caminho relativo, comprimento + 1 B, uma vez por inode.
- `xarray` por inode: nós de 64 posições (cerca de 576 B por nó). Com números de inode
  densos o custo é da ordem de 9 a 16 B por inode; com inodes esparsos (típico em ext4/XFS
  após muitas criações e remoções) cresce, no pior caso para centenas de B por inode.

**O orçamento contabiliza** `sizeof(struct)` + payload + `strlen(path)+1` (funções `charge`/
`uncharge`). **Não contabiliza** o arredondamento de slab, os nós do `xarray` nem as
alocações transitórias do `QUERY` (`kvcalloc(limit × sizeof(result))`, até 4096 × ~280 B ≈
1,1 MiB por consulta, mais 256 caminhos). Portanto `index_used` do `GET_INFO` **subestima a RAM
real**. Esta diferença é um achado a corrigir ou documentar.

### 2.2 Estimativa por inode (estimativa, não medida)

Seja A o número de atributos indexados por inode, P o caminho médio (~40 B) e V o payload médio
(~16 B, mistura de inteiros e strings curtas).

Contabilizado (o que o orçamento vê): `40 + (P+1) + A × (64 + V)` ≈ `80 + 80·A` B.

| Perfil (benchmark-harness §3) | A | Contabilizado por inode | Estimativa de RAM real por inode (slab + xarray) |
| --- | --- | --- | --- |
| 3 atributos | 3 | ~320 B | ~450 a 600 B |
| 8 atributos | 8 | ~720 B | ~1,0 a 1,2 KiB |
| 16 atributos | 16 | ~1,36 KiB | ~1,9 a 2,3 KiB |

Observação: `memory_id` UUID (36 B) e `namespace`/`agent`/`tag` em string elevam V. Com V = 40 B,
o perfil de 8 atributos passa de ~720 B para ~1,0 KiB contabilizados.

### 2.3 O que 128 MiB significam (estimativa, não medida)

`128 MiB = 134 217 728 B`, em número de inodes pelo contabilizado:

| A | inodes até saturar o orçamento (contabilizado) | inodes com RAM real estimada |
| --- | --- | --- |
| 3 | ~400 mil | ~230 a 300 mil |
| 8 | ~180 mil | ~110 a 130 mil |
| 16 | ~95 mil | ~55 a 70 mil |

Com strings maiores (V ≈ 100 B, p. ex. `tag` e `kind` verbosos), reduzir 30 a 40%. Conclusão
da derivação: o orçamento padrão cobre algo entre **dezenas e poucas centenas de milhares de
memórias**, o que **é suficiente** para memórias de agentes na escala de projeto
(milhares a dezenas de milhares) e **insuficiente** para corpora grandes. Observação sobre o
plano: a escala S6 do plano (50 a 65 mil inodes) pode não atingir 100% do orçamento com 3 a 8
atributos; para medir saturação será preciso reduzir `index_budget` (a opção aceita 1M a 4G) ou
ir além de S6. Isto deve ser decidido antes de executar o M4.

### 2.4 Comportamento perto da saturação (comprovado por leitura de código; saturação real não medida)

- **Montagem**: se a varredura estoura o orçamento, `index_put` devolve `-ENOMEM`, a varredura
  para e `moobl_index_build` falha com aviso (`mount with noindex to skip it`): montagem
  recusada, ou `noindex` (QUERY responde `EOPNOTSUPP`; `ENOSYS` até o registro 0050). Testado com `index_budget=1M` em árvore
  minúscula (registro 0014); com carga grande, não.
- **Em execução**: falha de memória ao atualizar o índice depois de uma escrita já feita no
  inferior **não desfaz a escrita**: marca `incomplete` e o `GET_INFO` expõe
  `MOOB_FLAG_INDEX_INCOMPLETE`; `QUERY` passa a dar respostas **incompletas até remontar**.
  Falha de `charge` ao registrar caminho é silenciosa (`index_set_path` mantém o nome antigo;
  a consulta recorre ao alias de dentry e pode omitir o resultado); a falha de atualização de
  entrada emite um `pr_warn` único e marca `incomplete`. Consequência prática: perto de 100% o sistema
  perde silenciosamente resultados em vez de falhar; o cliente precisa consultar `GET_INFO`.
- Campos do `GET_INFO`: `index_budget`, `index_used`, `index_entries`, `index_inodes`,
  `flags` (`INDEX_INCOMPLETE`, `PATHS_STALE`, `INDEX_OFF`).
- Não há teto por usuário nem verificação de contabilização em cgroup
  (`distribution-strategy` §7: "não verificado"). Mount múltiplo soma orçamentos.

### 2.5 Reconstrução

Percurso de toda a árvore + `listxattr` por arquivo + `getxattr` por atributo, linear no número de arquivos (todo arquivo é visitado, tenha ou não atributos), com o lock de escrita do índice retido. Cache frio: dominado por I/O de metadados (potencial de minutos em centenas de milhares de arquivos em disco lento, a medir em W6); cache quente: CPU (W7).

### 2.6 Comparação com SQLite (sem alegar economia)

SQLite mantém índices em disco; a RAM é o cache de páginas (configurável, por padrão ~2 MiB por
conexão) mais o processo da aplicação, e o sistema operacional cacheia o arquivo no page cache.
Uma linha de 8 colunas com ~5 a 6 índices ocupa tipicamente algumas centenas de bytes em
disco (estimativa, não medida). **Não é possível afirmar que o MoobFS economiza memória.**
Diferenças qualitativas:

- O índice do MoobFS **precisa estar inteiro em RAM do kernel** (não paginável, não
  descartável sob pressão) e é reconstruído a cada montagem. O SQLite pode atender consultas
  com working set menor que o banco e persiste sem reconstrução.
- O MoobFS elimina o processo de aplicação como dono do índice e a duplicação de caminho/valor
  entre tabela e arquivo, mas **duplica** o valor dos atributos entre xattr no disco e índice
  na RAM.
- Em hosts com muita memória e poucas dezenas de milhares de memórias, a diferença provavelmente
  é irrelevante. Em hosts pequenos, memória do kernel não paginável é pior que cache
  descartável. **Potencial desvantagem**, a medir via RSS + `index_used` versus RSS do SQLite
  em S1..S6.

---

## 3. Segurança e isolamento

### 3.1 O que é herdado do VFS e do inferior (não é mérito do MoobFS)

- uid/gid, bits de modo, ACLs POSIX, travessia de diretório, `inode_permission`,
  `security_inode_permission` (LSM), `CAP_DAC_*`. A camada delega: `moobl_permission` chama
  `inode_permission` no inode inferior (registro 0020). **Comprovado** que as decisões de
  acesso a arquivos pela camada coincidem com as do inferior nos testes de dois uids em ext4,
  btrfs e XFS.
- Confidencialidade em disco, criptografia, integridade de dados (checksums do btrfs), tudo do
  inferior.
- Root e quem acessa o diretório inferior contornam a camada (limite estrutural, documentado).

### 3.2 O que o MoobFS acrescenta

| Acréscimo | Mecanismo | Estado |
| --- | --- | --- |
| Autorização por resultado do `QUERY`, inclusive `count`/`limit`/`truncated` só sobre o visível | `caller_may_read`: `vfs_path_lookup` com as credenciais do chamador (exige busca em cada diretório), mesma montagem e superbloco, mesmo inode indexado, `inode_permission(MAY_READ)` | **comprovado** em teste local (dois uids, dois namespaces, 600 candidatos privados e 6 autorizados, ACLs, remontagem) em três inferiores; auditoria externa inexistente |
| Validação de tipo dos atributos no kernel | `moobl_typed_check` antes de tocar o inferior; `EINVAL`; `user.moob.format` reservado | **comprovado** (testes de entradas inválidas, fuzzing de codificação) |
| Recusa de mount idmapped e de mount em user namespace | `FS_ALLOW_IDMAP` e `FS_USERNS_MOUNT` não declarados; `EINVAL`/`EPERM` testados | **comprovado**; escolha conservadora, evita a classe de CVEs do overlayfs, ao custo de não funcionar em contêineres rootless |
| Exigência da raiz do mount para `QUERY`/`GET_INFO` | `file->f_path.dentry == sb->s_root` | **comprovado**; limitação de usabilidade em contêiner |

### 3.3 Vazamento de metadados: o que o `QUERY` poderia revelar e como é evitado

Um índice global em RAM, sem filtro, revelaria: existência de arquivos inacessíveis, seus
caminhos, o número de inodes e a contagem de acertos (canal lateral por `count`/`truncated`).
Era exatamente a falha da especificação inicial (`spec-m3` §1: "vazamento de existência e de
caminhos entre usuários"). A mitigação implementada: cada candidato é resolvido pelo VFS com as
credenciais do chamador e omitido em silêncio se negado; `count` e `truncated` só contam
visíveis (**comprovado** por teste). Riscos residuais **não eliminados**:

- **Canal de tempo**: a latência da consulta cresce com candidatos privados (varredura em lotes
  de 256 com uma resolução de caminho cada). Um usuário pode inferir, por tempo, quantos
  candidatos alheios existem. **Não demonstrado** se é explorável; não testado.
- **Atributos em xattr visíveis** seguem as permissões do arquivo (inferior); não há vazamento
  adicional se o arquivo é ilegível, mas o `QUERY` revela ao leitor os nomes que casam.
- **Falso negativo após rename de diretório** (`PATHS_STALE`): não vaza, mas omite; documentado.
- Handles para `open_by_handle_at` não são devolvidos na v1 atual (resultado traz inode e
  caminho); S11 do roadmap de segurança exige reverificar se isto mudar.

### 3.4 Contrato TOCTOU

Um resultado **não concede acesso**. A autorização vale no instante da consulta; o cliente faz
`open()` e deve aceitar `EACCES`/`ENOENT`. Não há handle especial. Isto é correto e honesto,
mas significa que o `QUERY` pode listar um nome que deixa de ser legível logo após a resposta
(vazamento de existência por um instante, não de conteúdo).

### 3.5 Concorrência entre `chmod`/`rename` e consultas

Teste adicionado a `vm/suite.sh` em 2026-10-10: 40 arquivos, um processo alterando `chmod`,
`rename` (arquivo e diretório) e `unlink`+recriação em laço enquanto `QUERY` roda como uid 1001
por 5 s; exige respostas bem formadas e igualdade final com o oráculo. **Executado em
2026-10-10 (registro 0020)**: cerca de 620 consultas por inferior, todos os resultados abertos
ou falhando só com `ENOENT`/`EACCES` depois da resposta (janela TOCTOU do contrato), resposta
final igual ao oráculo em ext4, btrfs e XFS, sem avisos de kernel. A ausência de vazamento sob
essa concorrência é **comprovada na escala do teste** (40 arquivos, um escritor, emulação);
o KASAN/lockdep sobre a branch continua pendente (execução disparada, sem resultado
registrado).

## 4. Confiabilidade e persistência

### 4.1 O que é delegado ao inferior (e portanto não é mérito nem demérito do MoobFS)

Persistência, journal, recuperação após queda, atomicidade de rename, checksums de dados,
reflink, dedup, ACLs. **Comprovado** (registro 0015): `dm-log-writes` com replay em cinco
marcas (`mkfs`, `m1`, `m2`, `m4`, `end`) mostrou, em ext4, `e2fsck` limpo e memórias até a
marca íntegras (conteúdo e atributos); `dm-flakey` com erros de escrita propagou o erro sem
`BUG`/`Oops`/`WARNING` e permitiu remontagem limpa. Limite: **um** cenário de carga, ext4 no
replay, VM única, `dm-flakey` só em ext4 e só no modo de erro de escrita; btrfs e XFS não
foram submetidos a replay.

### 4.2 Consistência arquivo × metadados

Conteúdo e xattr são operações distintas no inferior; não há transação. A convenção
(tmp + `setxattr` + `fsync` + `rename` + `fsync` do diretório) dá o nome final com conteúdo e
atributos completos **se o cliente a seguir**; evidenciada com `dm-log-writes`. Clientes que
escrevem in-place podem observar conteúdo novo com atributos antigos após queda. O SQLite, com
tabela e índice na mesma transação, **oferece atomicidade que o MoobFS não oferece** entre dado
e metadado, mas não entre arquivo externo e banco (o baseline de dois armazenamentos tem o
problema inverso: arquivo e linha não são atômicos entre si). O MoobFS troca o problema "arquivo
e linha" por "conteúdo e xattr", ambos com a ordem de gravação sob responsabilidade do cliente.

### 4.3 Concorrência e defeitos já encontrados

Três defeitos reais do próprio módulo foram achados e corrigidos (todos achados por teste, não
por revisão externa):

1. dentry superior negativo mantendo `d_fsdata` apontando para o inferior antigo: perda de
   quadros do WAL do SQLite (184 a 188 de 200 linhas) — **perda de dados**;
2. corrida em `set_nlink` por cópia de atributos sem lock: `WARNING` em `__destroy_inode` só
   visível no runner x86 de 2 CPUs, não na VM local sob TCG;
3. `rwsem`/`xarray` destruídos sem inicialização em montagens que falham cedo: só o kernel com
   `DEBUG_RWSEMS` acusou.

Leitura crítica: o defeito 1 era uma **perda de dados silenciosa em um cliente comum**, achada
porque se escolheu um cliente exigente; o 2 e o 3 foram achados por mudar o ambiente de teste.
Isto sugere que a taxa de defeitos residuais em caminhos ainda não exercitados é **não nula e
desconhecida**. Cobertura que falta: xfstests completo, syzkaller, longas execuções, outros
kernels.

### 4.4 Inferiores, kernel e empilhamento

ext4, btrfs e XFS: comprovado em VM (fsx de 9000 operações cada). Kernel: só Debian 6.12.111 e upstream 6.12.112 (sob demanda); a API interna já divergiu entre eles. Empilhar evita reinventar journal e alocador (comprovado por desenho) mas traz riscos próprios: aliasing de inode, coerência de dentries, mmap e domínios de lock distintos entre acesso pela camada e pelo inferior.

## 5. Agilidade de desenvolvimento

### 5.1 Onde reduz complexidade (potencial)

- **Um armazenamento em vez de dois**: arquivos com xattr tipado + `QUERY`, em vez de arquivos
  + banco que a aplicação mantém sincronizado (duas escritas, uma inconsistência possível, rotina
  de reparo). **Redução arquitetural plausível, não medida.**
- **Sem daemon**: nenhum processo de banco ou serviço por host. **Comprovado por desenho.**
- Qualquer ferramenta Unix lê os dados; backup é `rsync`/`git`. **Comprovado por desenho e para
  git/fsx**.
- A validação de tipo no kernel impede dado malformado no atributo, que o SQLite com colunas
  tipadas também impede (afinidade de tipo mais fraca, mas CHECK/STRICT disponíveis).

### 5.2 Onde acrescenta complexidade (comprovado)

- Linux-only; módulo de kernel a instalar e manter; montar exige `CAP_SYS_ADMIN` no namespace
  inicial; sem user namespace; sem idmapped mount; sem contêiner montando.
- **API simples não é integração simples**: o `QUERY` é um `ioctl` na raiz do mount. Não há
  CLI geral (`tests/query.c` é binário de teste), nem binding de linguagem, nem biblioteca.
  Um cliente em Python/Node/Go precisa escrever a estrutura `struct moob_query` (~4,4 KiB) e
  fazer o `ioctl` à mão. Um subdiretório com bind mount **não pode** emitir `QUERY`.
- Provisionamento (usuários, grupos, modos, ACLs, diretórios de namespace) é do administrador;
  nenhuma ferramenta existe. Identidade `(namespace, memory_id)` é convenção do cliente,
  verificada offline por `moobfs-check`.
- Modelo de consulta limitado: conjunção de até 4 termos, sem OR, sem ordenação, sem
  agregação, sem join, sem paginação por cursor externo (o retomar é interno). SQLite oferece
  SQL completo. Uma necessidade que o MoobFS não cobre obriga o cliente a voltar ao banco.
- Depuração: sem `EXPLAIN`, sem shell interativo.

## 6. Casos de uso

### Caso A: memória compartilhada multiagente em um host, com isolamento POSIX

- Problema real: vários agentes (uids distintos) compartilham um store; cada um só pode ver as
  memórias que o SO lhe permite, inclusive nas buscas.
- Solução convencional: SQLite/Postgres com camada de autorização na aplicação (ou um
  serviço), ou um diretório por agente sem busca cruzada.
- Uso do MoobFS: um mount por host, `/<ns>/shared/` e `/<ns>/agents/<a>/`, um uid por agente,
  `QUERY` filtrado pelas credenciais do chamador.
- Benefício esperado (potencial): consulta cruzada com **política de acesso única** (a do SO),
  sem segundo mecanismo de autorização para manter; ACLs funcionam.
- Métrica que prova: W3 com N uids e razão de candidatos privados 1:1 a 100:1 (p99 e CPU);
  zero vazamento (já testado em pequena escala) sob W5; custo de manter equivalência de política
  no baseline (linhas de código de autorização do lado do SQLite, medidas, não estimadas).
- Quando **não** é a melhor escolha: quando os agentes rodam em contêineres rootless ou sem
  acesso à raiz do mount; quando uma única aplicação confiável já faz a autorização; quando o
  corpus passa do orçamento do índice; quando a consulta exige OR, ordenação ou agregação.

### Caso B: memória que deve continuar legível por git/grep/rsync/backup

- Problema: auditar, versionar e restaurar memórias com ferramentas comuns, sem exportação.
- Convencional: arquivos markdown + git, sem busca por atributo; ou banco com exportação.
- Uso: camada sobre ext4/btrfs/XFS; desmontar e o diretório inferior é árvore comum.
- Benefício (comprovado por desenho): não há formato próprio; `moobfs-check` valida. O
  benefício vale também **sem a camada** com arquivos puros; o que a camada acrescenta é a
  consulta.
- Métrica: restauração a partir de `rsync`/`git` seguida de `moobfs-check` limpo; tempo para
  reconstruir índice (W6).
- Não é a melhor escolha se a consulta é raramente necessária: arquivos puros + `find` +
  `getfattr` bastam e não exigem módulo.

### Caso C: aplicação de processo único e agente único

- Problema: guardar memória de um agente.
- Convencional: arquivos ou SQLite. Um único processo dono do dado não precisa de autorização
  POSIX nem de consulta cruzada.
- **Probabilidade de valer a pena: baixa.** O custo de módulo, root e instalação não é pago por
  nenhum benefício; SQLite é mais rápido de adotar e mais maduro. Recomendação atual: SQLite
  ou arquivos.

---

## 7. Proposta de valor: matriz

| Benefício alegado | Mecanismo | Evidência atual | Evidência necessária | Baseline | Risco/trade-off | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Consulta por atributo mais rápida | rbtree em RAM no kernel, uma chamada | correção contra oráculo em árvore de 6 inodes | W3 p50/p95/p99 em S1..S6, com gate `EXPLAIN` | SQLite composto A/B | autorização por resultado e varredura linear podem anular o ganho | não demonstrado |
| Menos escrita / um único store | xattr + índice RAM sem segunda gravação | nenhuma | W1 e `WA_relative` (diskstats) | SQLite WAL + arquivo | lock global de índice; durabilidade igual à do inferior | potencial |
| Isolamento POSIX nas consultas | `vfs_path_lookup` + `inode_permission` por resultado | testes com 2 uids, ACLs, 3 inferiores (registro 0020) | W5 concorrente, KASAN, revisão externa, canal de tempo | autorização na aplicação | canal de tempo, TOCTOU, falso negativo após rename | comprovado em escala de teste; fase aberta |
| Nada preso | xattr reais, arquivos comuns | desenho + git/fsx/`moobfs-check` | restauração de backup com ferramentas padrão em corpus real | arquivos puros (empata); SQLite (arquivo legível por `sqlite3`) | índice não persiste | comprovado (desenho e testes) |
| Sem daemon | módulo no kernel | desenho | operar N semanas sem incidente | SQLite (também sem daemon, em processo) | falha no anel 0 em vez de no processo | comprovado como desenho; vantagem real só frente a Postgres |
| Recuperação entre sessões | remount reconstrói o índice | remount correto em árvore pequena | W6/W7 em S1..S6 | SQLite abre arquivo | cresce linear; segura lock | potencial (a favor: sem corrupção de índice; contra: tempo) |
| Escala por orçamento de 128 MiB | contabilidade `charge` | estimativa de ~10^5 inodes | saturação 50..100% medida | SQLite sem limite de RAM | respostas incompletas silenciosas perto do teto | não demonstrado |
| Compartilhamento físico de bytes | reflink/dedup do inferior | testes em btrfs/XFS | W-compartilhar com dataset real | cp --reflink / duperemove direto | não é mérito do MoobFS | herdado; não creditar |
| Compatibilidade com clientes reais | passagem completa | git, SQLite WAL (200 inserções), cliente próprio | ai-memory, mem0, Letta; xfstests | n/a | defeitos latentes (3 achados) | comprovado em pequena escala |

### 7.1 Resultados negativos do M4 e o que invalidam

| Resultado negativo | Argumento que cai |
| --- | --- |
| W3 p99 não é melhor que SQLite com índice composto, em nenhuma forma Q1..Q9 | O argumento de **velocidade de consulta** cai. Resta o argumento de autorização e de arquitetura (sem banco), que precisa ser reavaliado em termos de custo operacional, não de latência |
| W3 só vence em consultas de 1 termo seletivo e perde em multi-filtro | O argumento fica restrito a esse perfil; recomendação passa a "casos específicos" |
| Overhead de W1 > ~o ganho de eliminar a segunda escrita (diferença `WA_relative` ≥ 1 ou p99 pior) | O argumento de **caminho de escrita** cai |
| W2 com overhead que não se esconde atrás do cache de página | O argumento de "passagem barata" cai; passa a custo aceito por quem precisa da consulta |
| W6 cresce linear até minutos em S6 (cache frio) | O argumento de **recuperação entre sessões** enfraquece; exige índice persistente (decisão de não persistir cai) |
| 128 MiB cobrem menos que ~dezenas de milhares de memórias no perfil 8/16 | O argumento de **escala** cai para o orçamento padrão; só vale com orçamento maior (RAM não paginável) |
| W5/W8: vazamento, `WARNING` ou p99 de escrita degradado sob consultas largas (lock global) | O argumento de **multiagente concorrente** e de isolamento cai até corrigir |
| Custo da autorização (consulta como root versus usuário restrito) ≫ custo do índice | O ganho de "autorização gratuita" desaparece; vale só se o custo for menor que uma camada de autorização equivalente na aplicação (medir essa também) |
| Cliente real inalterado falha ou degrada (ai-memory, Letta, mem0) | O argumento de **adoção sem modificação** cai |

---

## 8. Critérios de sucesso

Limiares justificados por custo operacional e perfil de carga, não por percentuais arbitrários.
Todos medidos no harness (`benchmark-harness.md`): mínimo 7 repetições, alternância de
ordem, dispersão >20% marca "inconclusivo", cada tabela cita commit, comando e
`results.csv`.

### 8.1 Viabilidade técnica

- Equivalência `bench/equiv` kernel × SQLite × oráculo em 100% das consultas; nenhum resultado
  truncado contado como completo.
- Zero `BUG`/`Oops`/`WARNING`/KASAN/lockdep em toda a suíte e no M4.
- `GET_INFO` consistente com contagem real (diferença entre `index_used` e RAM real medida e
  documentada ou contabilidade corrigida).
- Critério de saída: as quatro perguntas do plano respondidas por números, **inclusive onde a
  camada perde**.

### 8.2 Vantagem de desempenho

- Limiar não é "X% mais rápido": é **p99 de uma consulta W3 abaixo do orçamento de latência da
  chamada do agente** (a consulta precede uma chamada de LLM que custa centenas de ms a
  segundos, portanto diferenças de microssegundos são irrelevantes ao usuário) **e** não pior
  que o baseline mais maduro. Ganho relevante exige diferença que sobreviva à dispersão
  (p5-p95 entre repetições) em pelo menos dois perfis e duas escalas.
- Para escrita: `WA_relative` < 1 com durabilidade equivalente, ou overhead de p99 de W1 dentro
  do que o cliente já tolera (a escrita de memória costuma ser assíncrona ao fluxo do agente).
- Para W2: overhead de passagem reportado como razão com intervalo; aceitável se a
  incerteza é menor que a variação normal entre ext4 e btrfs no mesmo hardware.
- Para W6: tempo de montagem em S6 comparável ao tempo que o operador já tolera para reiniciar o
  serviço (ex.: dezenas de segundos), com limite explícito em arquivos por segundo.

### 8.3 Maturidade de produção

Vinculada a `docs/security-roadmap.md`: S1 (modelo de ameaça), S2 (xfstests), S3 (syzkaller),
S4 (inferior hostil), S5 (análise estática), S6 (divulgação), S8 (hardening), S9 (limites e
DoS), S10 (assinatura/DKMS); mais KASAN/lockdep contínuos na branch ativa e matriz de ao menos
duas séries de kernel. Sem isso, nenhuma recomendação de produção.

---

## 9. Maturidade tecnológica como critério de adoção

### 9.1 Princípio

Adotar o MoobFS precisa vencer **não só o desempenho das alternativas, mas o risco e o custo de
introduzir um módulo de kernel**. A maturidade de SQLite, PostgreSQL, ext4, btrfs e XFS é uma
vantagem concreta deles, não um detalhe. Um ganho pequeno de desempenho **não justifica** um
módulo de kernel: o preço de um defeito no anel 0 (panic do host, corrupção da árvore
inferior) é desproporcional ao de um defeito num processo. Não se credita ao MoobFS o que o
Linux ou o inferior já oferecem, e não se penaliza o SQLite com configuração ruim: assume-se
WAL, índices compostos e `ANALYZE`, como em `benchmark-harness.md` §4.

### 9.2 Oito fatores

**1. Maturidade e histórico.**
- SQLite: mais de duas décadas, ubíquo, suíte de testes extensa (TH3, cobertura de ramos
  próxima de 100%, fuzzing contínuo), operação em escala imensa. ext4/XFS/btrfs: anos de
  produção, xfstests, relatórios de maintainers, fuzzing de comunidade e CI de distribuições.
- MoobFS: meses, um autor, um kernel (6.12.111, mais 6.12.112 sob demanda), três defeitos de
  kernel encontrados pelos **próprios** testes, nenhum usuário externo, sem reprodução
  independente. Comprovado.
- Veredito: ampla vantagem das alternativas.

**2. Superfície de risco.**
- SQLite: falha derruba um processo (o da aplicação); o dado está num arquivo com formato
  documentado e ferramenta de verificação (`PRAGMA integrity_check`).
- MoobFS: falha pode causar panic do host, travamento de montagem, ou corromper/perder dados
  da árvore inferior (o defeito 1 foi perda silenciosa). Um defeito disparável por usuário
  sem privilégio é DoS do host inteiro. Raio de dano: o host e todos os serviços nele.
- Veredito: risco do MoobFS maior em magnitude e menor em testes. Comprovado por natureza do
  anel 0 e pela história dos três defeitos.

**3. Manutenção.**
- SQLite: biblioteca estável, ABI e formato de arquivo compatíveis por décadas; atualiza-se
  com o pacote da aplicação.
- MoobFS: a API interna do kernel não é estável. Já houve divergência entre 6.12.111 e
  6.12.112 (assinatura de `backing_file_open`); a série 6.x alterou `backing-file`, a
  assinatura de `inode_permission`/`mnt_idmap` e a API de ACL (`get_acl`/`set_acl` para
  `vfs_get_acl`/`vfs_set_acl`, com `struct mnt_idmap`) em várias versões. Cada atualização de
  kernel de distribuição exige recompilar (DKMS) e revalidar; falhas de DKMS deixam o host sem
  montagem ou impedem boot de serviços dependentes. Secure Boot exige assinatura (MOK) e
  lockdown; S10 do roadmap não está feito. Nenhuma matriz de kernels além de um ponto.
- Veredito: custo contínuo de manutenção significativo e sem pessoas para absorvê-lo.

**4. Segurança.**
- Classe de referência: overlayfs e ecryptfs (também empilhados) acumulam CVEs relevantes
  (2015-1328, 2016-1576, 2020-16120, 2026-74619 no overlayfs; 2009-2406 e 2014-9922 no
  ecryptfs, conforme `docs/history/0017`). Isso é evidência de que sistemas empilhados têm uma
  taxa histórica de vulnerabilidades, mesmo com anos de revisão pública.
- SQLite: falhas ocorrem, mas contidas no processo; permissões e isolamento são os do SO sobre
  um arquivo.
- MoobFS: mitigação por escolha (userns e idmap recusados) e validação de entrada; porém sem
  syzkaller, sem fstests, sem modelo de ameaça escrito, sem auditoria externa. Host impactado
  em qualquer falha. Auditoria/log: nenhuma além de `dmesg`.
- Veredito: alternativas mais seguras hoje; o MoobFS tem um desenho de autorização
  interessante, ainda não sob revisão adversarial.

**5. Confiabilidade.**
- SQLite: décadas de produção, recuperação de queda de energia bem estudada, atomicidade
  transacional. Inferiores: journal e recuperação bem estudados.
- MoobFS: dm-log-writes em cinco marcas numa VM, ext4; `dm-flakey` em ext4; nenhum run
  prolongado; nenhum teste de falta de memória do índice em escala; confiabilidade da
  persistência é a do inferior, mas o caminho empilhado (dentries, mmap, locks) não tem a
  mesma história.
- Veredito: persistência herdada é sólida; a camada acrescenta risco, não confiabilidade.

**6. Observabilidade.**
- SQLite: `EXPLAIN QUERY PLAN`, shell `sqlite3`, `PRAGMA`, estatísticas, ecossistema amplo.
  Filesystems: `/proc`, `perf`, tracepoints, `lock_stat`.
- MoobFS: `GET_INFO`, `moobfs-check`, `dmesg`. Nada de métricas de latência, de contagem de
  consultas, de taxa de omissão por autorização, de tempo de montagem exposto. Sem tracepoints
  próprios. Diagnóstico de lentidão depende de `perf` externo.
- Veredito: observabilidade do MoobFS é mínima; é custo operacional real.

**7. Custo total.**
- SQLite: instalar (nada), atualizar (com a aplicação), monitorar (conhecido), suporte
  (comunidade vasta), treinamento (SQL é universal), incidentes (contidos).
- MoobFS: instalar módulo (DKMS, assinatura), atualizar a cada kernel, monitorar (precisa
  construir), suporte (um autor), treinamento (UAPI própria, regras do inferior, convenções),
  incidentes (risco no host). Mais o custo das **ferramentas de operação futuras** da camada:
  CLI `moobfs`, provisionamento, bindings, empacotamento, pipeline de teste por kernel: todas
  inexistentes e sob responsabilidade de quem adota ou do projeto.
- Veredito: custo total maior hoje; precisa ser pago por um benefício que ainda não se mediu.

**8. Reversibilidade.**
- MoobFS: **ponto forte comprovado por desenho e testes**. Desmontar deixa arquivos comuns com
  xattr legíveis por qualquer ferramenta; não há formato próprio; `moobfs-check` valida. Sair do
  MoobFS é `umount`.
- SQLite: o arquivo do banco permanece legível por qualquer `sqlite3`; reversão também é
  simples, mas o conteúdo está dentro de um único arquivo de formato próprio (documentado e
  estável) e os dados de memória só são lidos via SQL. Arquivos puros: reversão trivial.
- Veredito: empate técnico com arquivos puros; MoobFS vence o SQLite em "legível sem
  nenhuma ferramenta específica", mas SQLite é legível por uma ferramenta universal. Não é
  vantagem decisiva.

### 9.3 Separação obrigatória do que existe e do que não existe

| Dimensão | Situação |
| --- | --- |
| Ganho de desempenho medido | **nenhum** (zero números) |
| Ganho de produtividade demonstrado | **nenhum** |
| Simplificação arquitetural real | **plausível, não medida**: um armazenamento em vez de arquivos + banco; sem daemon; política de acesso única |
| Risco adicional de manutenção | **certo**: módulo fora da árvore, um kernel, DKMS, API instável |
| Risco adicional de segurança | **certo**: código no anel 0 com ioctl e xattr acessíveis a usuários; sem fuzzing de kernel, sem modelo de ameaça escrito, sem auditoria |
| Custo operacional | **maior hoje**: instalação, assinatura, observabilidade mínima, sem ferramentas |
| Maturidade de produção | **inexistente**: um autor, uma VM, três defeitos achados internamente |

### 9.4 Capacidades arquiteturais que poderiam justificar adoção mesmo sem vencer o SQLite em todo benchmark

1. **Autorização POSIX por resultado de consulta, em muitos uids, sem camada de autorização na
   aplicação**: a política é a do SO (modo, grupo, ACL, travessia), uma só. Reproduzir isso com
   SQLite exige que a aplicação reimplemente permissões ou que cada uid tenha banco próprio
   (perdendo consulta cruzada). Vale se o número de uids/agentes é alto e a política muda.
2. **Dados "sem nada preso", legíveis por toda ferramenta Unix** (git, grep, rsync, backup,
   reflink), com o mesmo acervo consultável por atributos. Vale quando auditoria e ferramentas
   padrão são requisito.
3. **Sem daemon nem processo de banco por host**: um módulo e o próprio sistema de arquivos.
   Vale quando o custo de operar um serviço por host é maior que o de manter um módulo (raro
   com SQLite embutido, mas relevante frente a PostgreSQL).

Nenhuma dessas é desempenho. Cada uma tem de ser comparada com seu custo (módulo de kernel) e só
conta se houver cenário que a exija **e** evidência de que a implementação é segura.

### 9.5 Matriz de decisão por cenário

| Cenário | Evidência indispensável antes de recomendar o MoobFS | Veredito hoje |
| --- | --- | --- |
| Laboratório / pesquisa | M4 completo (W1..W9) com relatório que inclui onde a camada perde; xfstests `quick` sobre a camada; KASAN limpo sobre a branch; teste de concorrência de `chmod`/`rename` com `QUERY` passando | **Aceitável**: experimentar em VM descartável é o uso previsto; recomendação de usar para obter números e feedback, não como armazenamento de dado de valor |
| Produção controlada, usuários especialistas | Tudo acima, mais: dois ou mais kernels em CI; syzkaller sobre ioctl/xattr com cobertura definida; modelo de ameaça (S1) e processo de divulgação (S6); DKMS e assinatura (S10); reprodução independente do M4; piloto de meses sem perda de dados; observabilidade mínima (métricas, tracepoints); runbook de recuperação | **SQLite ou arquivos puros permanecem a recomendação hoje.** O MoobFS só é justificável nesse cenário se a capacidade 9.4(1) for requisito real, com piloto isolado e plano de reversão (que existe por desenho) |
| Produção corporativa crítica | Todos os critérios de 9.6 satisfeitos; revisão externa de segurança; suporte contratável; matriz de kernels de distribuição suportadas; histórico de 12+ meses sem bug de perda de dados | **SQLite (ou PostgreSQL) e arquivos puros permanecem a recomendação hoje, sem ressalva.** Introduzir um módulo de kernel de um único autor nesse contexto não é defensável |

### 9.6 Fechamento da seção

**Vantagens reais das alternativas maduras.** Décadas de produção e de testes (SQLite,
inferiores); SQL completo (OR, ordenação, agregação, join); formato estável e portátil entre
sistemas operacionais; sem root, sem módulo, funciona em contêiner rootless, em CI hospedado e
em macOS/Windows; observabilidade rica; suporte e treinamento amplos; falhas contidas no
processo; transações atômicas sobre tabela e índice.

**Riscos específicos de adotar o MoobFS.** Panic do host ou perda de dados por defeito do
módulo; DoS por usuário sem privilégio; quebra a cada atualização de kernel; dependência de um
autor; impossibilidade de uso fora de Linux com root; índice limitado por RAM não paginável e
sem persistência; respostas incompletas silenciosas perto do orçamento; observabilidade mínima;
canal lateral de tempo não avaliado.

**Evidência já presente no repositório.** Correção das respostas do `QUERY` contra um oráculo
(escala mínima); isolamento por uid/ACL em três inferiores (escala de teste); git, SQLite WAL e
cliente próprio rodando sem modificação; replay de queda em cinco marcas; erros de disco
propagados; recusa de userns/idmap testada; três defeitos de kernel corrigidos com testes de
regressão; fuzzing do parser de codificação.

**Lacunas de validação.** Todo o M4; xfstests completo; KASAN sobre a branch atual e ciclos
contínuos; syzkaller; teste de `chmod`/`rename` concorrentes com `QUERY` (escrito, não
executado); canal de tempo; saturação do índice em escala; mais de um kernel; reprodução
independente; auditoria externa; clientes reais de agentes; operação de longa duração.

**Condições em que o MoobFS seria recomendável.** Muitos uids/agentes num mesmo host Linux
com root disponível, necessidade de consulta cruzada por atributos com política de acesso única
do SO, requisito de dados legíveis por ferramentas Unix, corpus dentro do orçamento do índice,
e M4 mostrando consulta e escrita no mínimo equivalentes ao SQLite, **mais** os critérios
abaixo cumpridos.

**Condições em que SQLite ou arquivos puros permanecem a recomendação.** Agente único ou
aplicação confiável única; qualquer cenário sem root ou sem controle do kernel (CI hospedado,
rootless, macOS/Windows, serverless); necessidade de OR, ordenação, agregação, busca textual
ou vetorial; corpus grande; ambiente corporativo crítico; equipe sem capacidade de manter
módulo; qualquer caso em que o ganho é só uma fração de desempenho.

**Critérios objetivos para passar de projeto experimental a tecnologia confiável.**
1. xfstests: passe completo (grupos `generic` e `quick` aplicáveis a filesystems empilhados),
   com lista de exclusões justificada, nos três inferiores.
2. KASAN/UBSAN/lockdep em CI contínuo na branch principal, sem diagnóstico por um período
   definido, e ciclos de syzkaller com cobertura de `layer/` acima de um limiar e zero
   relatos abertos.
3. Pelo menos duas séries de kernel suportadas (por exemplo, duas LTS) em CI, mais um kernel
   de distribuição com DKMS e Secure Boot validado.
4. Doze meses de uso real sem bug de perda ou corrupção de dados, em pelo menos dois
   ambientes independentes.
5. Reprodução independente dos resultados do M4 por terceiros, em outro hardware.
6. Itens do `security-roadmap.md` concluídos: S1 a S6, S8, S9, S10 (no mínimo).
7. Revisão externa de segurança e de código do módulo, publicada.
8. Mantenedores além do autor original.

---

## Entrega

### As cinco maiores vantagens potenciais

1. Consulta por atributos cuja autorização é a do SO (modo, grupo, ACL) por resultado, sem
   camada de autorização separada na aplicação (comprovado em escala de teste; custo não
   medido).
2. Um armazenamento em vez de arquivos mais banco, eliminando a segunda gravação e o problema
   de manter ambos consistentes (potencial).
3. Dados legíveis por qualquer ferramenta Unix sem a camada; reversão por `umount`
   (comprovado por desenho e testes).
4. Sem processo de banco por host (comprovado por desenho; vantagem real sobretudo frente a
   servidores de banco).
5. Reuso integral de persistência, reflink, dedup e ACLs de ext4/btrfs/XFS (herdado, não mérito
   próprio, mas barateia o projeto).

### As cinco maiores limitações

1. Nenhuma medição de desempenho existe; toda vantagem de velocidade é hipótese.
2. Módulo de kernel: risco de panic e de perda de dados, DoS por usuário comum, manutenção
   por versão de kernel; três defeitos achados pelos próprios testes, um deles perda de dados.
3. Índice só em RAM, não persistente, reconstruído por varredura linear; orçamento de 128 MiB
   cobre da ordem de 10^5 inodes (estimativa); perto do teto respostas incompletas silenciosas.
4. Consulta limitada (conjunção de até 4 termos, sem OR/ordenação/agregação) e varredura
   linear do primeiro termo, sem índice composto.
5. Linux com root e `CAP_SYS_ADMIN` inicial; sem contêiner rootless, userns, idmap; sem CLI,
   bindings, provisionamento, observabilidade; um autor, um kernel.

### Comparação honesta

| Critério | Arquivos puros | SQLite ao lado dos arquivos (WAL, índices compostos, ANALYZE) | MoobFS |
| --- | --- | --- | --- |
| Consulta por atributo | `find` + `getfattr`, linear | Índice composto, SQL completo (comprovado, maduro) | `QUERY` conjuntivo, varredura em RAM (correto; velocidade não medida) |
| Autorização da consulta | POSIX por arquivo, sem consulta | Na aplicação | POSIX por resultado (comprovado em teste) |
| Latência de leitura por caminho | Referência | Referência (arquivo) | Referência + salto de VFS (certo, magnitude desconhecida) |
| Escrita de memória | Referência | Arquivo + linha (2 gravações) | Arquivo + xattr + RAM (potencial menor amplificação; não medido) |
| Recuperação entre sessões | Nada a reconstruir | Abre arquivo | Varredura linear na montagem |
| Memória | Nenhuma | Cache de páginas, paginável | Índice não paginável (estimativa ~0,3 a 2 KiB/inode) |
| Maturidade | Máxima | Décadas | Meses, um autor |
| Risco de falha | Mínimo | Processo | Host |
| Instalação | Nenhuma | Biblioteca | Módulo, root, assinatura |
| Portabilidade | Total | Total | Linux 6.12 em VM de teste |
| Reversibilidade | Total | Alta (arquivo de formato estável) | Total (comprovado) |
| Observabilidade | `ls`, `getfattr` | `EXPLAIN`, shell | `GET_INFO`, `moobfs-check`, `dmesg` |
| Atomicidade dado × metadado | Não | Sim (dentro do banco) | Não (convenção do cliente) |

### Casos em que o MoobFS pode vencer

Muitos agentes de uids distintos num host Linux com consulta cruzada sob política POSIX única; acervo auditável por git/grep/rsync e consultável por atributos; frameworks em markdown que querem evitar um banco só para filtros simples; hosts com root e kernel fixado.

### Casos em que provavelmente não vale a pena

Agente único; dono único confiável; ambientes sem root (CI hospedado, rootless, serverless, macOS/Windows); necessidade de OR, ordenação, agregação, texto ou vetores; corpus grande; produção corporativa crítica; equipes sem capacidade de manter módulo.

### Medições do M4 indispensáveis

W3 (p50/p95/p99 em todas as formas Q1..Q9 com gate de `EXPLAIN`, incluindo custo da
autorização isolado); W1 e `WA_relative` com durabilidade equivalente; W2 (overhead de
passagem); W6/W7 em S1..S6 (cache frio e quente); saturação 50..100% do orçamento com
RAM real versus `index_used`; W5/W8 (concorrência e lock global); cliente real inalterado
(ai-memory ou equivalente); `chmod`/`rename` concorrentes com `QUERY` sob KASAN; canal de tempo
da autorização.

### Proposta de valor técnica (sem afirmação não comprovada)

O MoobFS é um módulo de kernel experimental que empilha um índice de atributos tipados em RAM
sobre ext4, btrfs ou XFS. Os arquivos continuam sendo arquivos comuns com xattr, legíveis sem a
camada. Uma consulta por ioctl devolve apenas o que o chamador teria permissão de abrir pelo
VFS, inclusive via ACL. A persistência, o journal, o reflink e o dedup são do inferior. Não há
processo de banco. Até o momento, o que está demonstrado é a correção das respostas e o
isolamento em escala de teste; o desempenho, a escala real e a operação prolongada não foram
medidos.

### Conclusão fundamentada

O problema que o MoobFS pretende resolver melhor do que as alternativas é estreito: **consulta
por atributos com autorização POSIX por resultado sobre muitos usuários Unix, mantendo os dados
como arquivos comuns, sem aplicação de autorização separada e sem servidor de banco**. Essa
proposição é coerente com o desenho e tem evidência de correção em escala de teste, mas
**ainda não é demonstrável como vantagem real**: falta qualquer medição que mostre que ela
custa menos (em latência, em escrita, em memória, em operação e em risco) do que SQLite com
índices compostos mais autorização na aplicação, e o preço de entrada (módulo de kernel de um
autor, em um kernel, com três defeitos já encontrados) é alto. Hoje, para qualquer cenário que
não seja laboratório, **SQLite ou arquivos puros são a recomendação**. O MoobFS merece
continuar como pesquisa até o M4 e os critérios de 9.6; um resultado em que a camada não mostre
vantagem em nenhuma das quatro perguntas do plano é válido e deve mudar o rumo do projeto, como
já prevê a ADR-0007.
