# Especificação: `QUERY` por domínio (UAPI v2)

Estado: **especificação aceita**, 2026-10-11, decorrente da ADR-0009 (alternativa E, aceita). Os quatro
pontos marcados **[decisão aceita]** foram aceitos pelo mantenedor no mesmo dia, nas formas recomendadas:
domínios por `domain_depth`, ACLs estendidas fora da partição nesta versão, v1 restrita a
`CAP_SYS_ADMIN`, e `EXDEV` para diretórios renomeados entre domínios. Implementação por fases (§8).

## 1. Objetivo

Responder consultas por atributo dentro de um domínio de permissão sem percorrer entradas de
outros domínios, sem verificar o caminho de cada resultado e sem motor de banco de dados. Fecha o
canal de tempo do registro 0030, remove o custo por resultado do registro 0028 e limita o custo
por candidato ao tamanho do domínio (registro 0029).

## 2. Domínios

- **Definição.** Um domínio é um diretório numa profundidade fixa abaixo da raiz da montagem,
  dada pela opção de montagem `domain_depth=N` (padrão 1: cada diretório de primeiro nível é um
  domínio; 0 desliga os domínios e mantém só o comportamento da v1). **[decisão aceita]** Alternativa
  considerada: marcar domínios com um atributo reservado; descartada porque qualquer dono de
  diretório poderia criar ou desfazer domínios sem o administrador.
- **Partição.** O índice em RAM passa a ter uma partição por domínio (árvore de nomes de atributo
  e, por nome, árvore de entradas ordenadas por tipo, valor e inode), mais uma partição para o que
  estiver fora de qualquer domínio (acima da profundidade N). O mapa de inode para registro
  continua global e guarda o domínio de cada inode.
- **Orçamento.** O `index_budget` continua sendo da montagem inteira; o `GET_INFO` passa a
  informar o número de domínios.

## 3. Regra de homogeneidade

Um arquivo só entra na partição do seu domínio se todo processo capaz de atravessar o diretório
do domínio também puder lê-lo. Como o kernel não enumera processos, a regra é verificada por
condições suficientes, avaliadas sobre o inode do arquivo `f` e o do diretório do domínio `D`:

1. `f` concede leitura a "outros" (`o+r`) e não tem ACL de acesso; **ou**
2. `f` e `D` têm o mesmo dono e o mesmo grupo, nenhum dos dois tem ACL de acesso além da mínima,
   e, para cada classe (dono, grupo, outros), se `D` concede execução àquela classe, `f` concede
   leitura a ela.

Arquivos com ACL de acesso estendida ficam fora da partição nesta versão, salvo pela condição 1.
**[decisão aceita]** Tratar ACLs exigiria comparar entradas nomeadas; fica para uma revisão com teste
próprio.

Um arquivo que não satisfaz a regra é **heterogêneo**: não aparece em consultas v2, é contado em
`GET_INFO` (`heterogeneous`) e listado pelo `moobfs-check`. A regra é reavaliada na mesma chamada
que muda o que ela usa: `setattr` (modo, dono, grupo), `set_acl` e criação, sob o lock de escrita
do índice. Mudança de modo, dono, grupo ou ACL do próprio `D` marca a partição como "a
reavaliar"; a consulta seguinte a essa partição reavalia todas as entradas antes de responder
(custo linear no tamanho do domínio, pago uma vez por mudança).

**Implementação Q2a (2026-10-11, registro 0035), diferenças em relação ao texto acima.** A
proposta continua registrada como acima; o comportamento implementado é este:

- Mudança de modo, dono, grupo ou ACL de um **diretório** na profundidade do domínio ou abaixo
  dela (o próprio `D` ou um intermediário) não reavalia a partição: o domínio passa a responder
  com verificação de permissão por resultado (como a v1) até a próxima montagem, e `GET_INFO`
  conta esses domínios em `fallback_domains`. Motivo: reavaliar exigiria percorrer a subárvore
  sob o lock de escrita do índice; o modo de contingência é correto, mais simples e raro em uso
  normal (diretórios de agentes não mudam de permissão a cada escrita).
- Diretórios intermediários entre `D` e o arquivo entram na regra pelo bit de execução: cada um
  deve deixar atravessar todo processo que atravessa `D` (condição 1 com `o+x`, ou condição 2
  com execução no lugar de leitura).
- Arquivo regular com mais de um nome (`nlink > 1`) é heterogêneo, porque outro nome pode estar
  em outro domínio. Ao voltar a um nome, ele pode continuar heterogêneo até a próxima mudança de
  modo, dono, grupo, ACL ou renomeação, ou até a próxima montagem (falha segura: fica de fora,
  nunca aparece a mais).
- Com `domain_depth=0` a v1 mantém a semântica anterior e continua aberta a todos; a restrição
  a `CAP_SYS_ADMIN` (§4.5) vale quando os domínios estão ligados.
- `MOOB_IOC_QUERY2` com o índice desligado (`noindex`) devolve `ENOSYS`, como a v1. (Revisão de 2026-10-12, registro 0050: as duas versões passaram a devolver `EOPNOTSUPP`.) Campos de
  `order_by`, `order`, `cursor`, `flags`, preenchimento, reservados e a marca `MISSING_OK` devem
  ser zero nesta fase (`EINVAL`); entram nas fases Q2b e Q2d.

## 4. Interface (UAPI v2)

### 4.1 `MOOB_IOC_QUERY2`

Emitido sobre um descritor aberto do **diretório do domínio** (não da raiz). Outro descritor
devolve `ENOTTY`; domínios desligados (`domain_depth=0`), `EOPNOTSUPP`.

Campos novos em relação à v1:

- `version = 2`.
- Até 4 termos com os operadores da v1, mais a marca por termo `MOOB_TERM_MISSING_OK`: o termo
  também casa entradas que não têm o atributo (validade e expiração do tipo "ausente ou maior
  que"). Ao menos um termo deve ser sem essa marca, salvo quando há `order_by`.
- `order_by` opcional: nome de um atributo do tipo `int64`, `time`, `double` ou `string`, e
  `order` (`ASC` ou `DESC`). Sem `order_by`, a ordem é a da varredura (não especificada).
- `limit` de 1 a 4096 e `cursor` opaco de 128 bytes: a resposta devolve no `cursor` a posição
  seguinte quando `truncated = 1`; reenviar a mesma consulta com esse `cursor` continua dali. Um
  cursor de outra consulta, de outra montagem ou após reavaliação da partição devolve `ESTALE`.
- Resultados: `ino`, `flags` e caminho **relativo ao diretório do domínio**.

**Implementação Q2b (2026-10-11, registro 0036), complementos ao texto acima.**

- O tipo de ordenação é explícito: campo `order_type` (`string`, `int64`, `double` ou `time`),
  tirado do preenchimento da estrutura, sem mudar o tamanho. Um atributo pode ter entradas de
  tipos diferentes; só as do tipo pedido entram na ordem. Isso torna "tipo errado" um erro de
  entrada verificável (`EINVAL`), enquanto `order_by` com um nome válido que nenhum arquivo do
  domínio tem devolve resposta vazia, e não erro: a ausência de um atributo não é um pedido
  malformado.
- Empates no valor são desfeitos pelo número do inode, no mesmo sentido da ordem.
- O cursor é a chave (tipo, valor, inode) do último resultado entregue, mais um resumo da
  consulta (domínio, estado de fallback, ordenação e termos; o `limit` pode mudar entre
  páginas) e um código de autenticação, ambos com uma chave aleatória da montagem. Corrompido,
  de outra consulta, de outra montagem ou emitido antes de o domínio entrar em fallback:
  `ESTALE`. Valores de até 96 bytes vão no cursor; os maiores são recuperados da entrada atual
  do inode, que precisa ainda ter o mesmo tipo, tamanho e prefixo, senão `ESTALE`.
- A paginação é por chave, como em bancos de dados: entradas criadas, removidas ou com valor
  alterado entre páginas podem aparecer duas vezes ou não aparecer; as que não mudaram aparecem
  exatamente uma vez.
- Sem `order_by`, o cursor também funciona, sobre a ordem de varredura do primeiro termo.

### 4.2 Autorização

Ao receber a chamada, o kernel verifica `MAY_EXEC | MAY_READ` no diretório do domínio com as
credenciais do chamador (os ancestrais foram verificados quando o descritor foi aberto). Se
passar, os resultados vêm da partição sem verificação por resultado: a regra de homogeneidade
garante que quem atravessa o domínio pode ler cada arquivo indexado nele. Contrato de tempo
(TOCTOU): igual ao da v1; uma mudança concorrente de permissão vale a partir da chamada seguinte.

### 4.3 `MOOB_IOC_QUERY2_BATCH`

Até 8 consultas v2 sobre o mesmo domínio numa chamada, com resultados concatenados e uma contagem
por consulta. União e remoção de duplicatas ficam com o cliente. Cobre o "ou" entre valores
(tags, tipos) com uma chamada.

**Implementação Q2d (2026-10-11, registro 0038), diferenças e complementos.**

- O lote leva um vetor de pedidos `moob_query2` completos, cada um com seu vetor de
  resultados, `limit`, `count`, `truncated` e `cursor`, em vez de um vetor único de resultados
  concatenados. O cliente que quiser os resultados contíguos aponta os pedidos para fatias de
  um mesmo vetor. Motivo: cada pedido continua paginável pelo próprio cursor, sem uma regra de
  divisão do limite entre pedidos.
- Todos os pedidos são validados antes de qualquer execução; um pedido inválido recusa o lote
  inteiro com `EINVAL` e nada é escrito de volta. Os pedidos rodam em ordem, cada um sob a sua
  própria aquisição do lock do índice: o lote não é uma fotografia única.
- `MOOB_TERM_MISSING_OK` fica no byte de marcas do termo (antes `reserved[0]`, renomeado
  `flags`). A v1 recusa qualquer marca (`EINVAL`). `EXISTS` com a marca é recusado ("existe ou
  ausente" casaria tudo). Um atributo presente com outro tipo, inválido ou bruto (`raw`) conta
  como presente ou ausente conforme o índice: tipo diferente é presente e não casa; inválido e
  bruto não são indexados e contam como ausentes.
- Na varredura simples, o kernel conduz pelo primeiro termo sem a marca (numa cópia dos termos;
  o resumo do cursor usa a ordem do pedido). A interseção usa só termos `EQ` sem a marca.

### 4.4 Plano de execução no kernel, sem planejador

- Com `order_by`: percorre a árvore do atributo de ordenação na partição, na ordem pedida, e
  testa os termos em cada entrada até completar o `limit`.
- Sem `order_by` e com dois ou mais termos de igualdade sem `MISSING_OK`: interseção por avanço
  alternado (leapfrog) sobre as sequências desses termos, que já estão ordenadas por inode dentro
  de cada valor; os demais termos são testados em cada inode da interseção.
- Nos outros casos: varredura do primeiro termo sem `MISSING_OK`, como na v1 corrigida.

**Implementação Q2c (2026-10-11, registro 0037).** A interseção vale para o `QUERY2` sem
`order_by` com dois ou mais termos `EQ`; a v1 continua com a varredura do primeiro termo, para
servir de base de comparação. O primeiro termo `EQ`, na ordem do pedido, conduz: os resultados
saem em ordem de inode e o cursor guarda o valor desse termo com o último inode. Cada passo é
uma busca logarítmica na árvore de cada termo, até todos chegarem ao mesmo inode; os demais
termos (inclusive os `EQ`, por simplicidade) são testados só nos inodes da interseção.

**Revisão do plano de igualdade (2026-10-12, registro 0044).** Medido no host do registro 0042,
o avanço alternado só ganhou quando a sequência do primeiro termo era longa e foi 2,7 a 3,6 vezes
mais lento que a varredura simples quando ela era curta. O plano passou a ser: medir as
sequências dos termos `EQ` em paralelo até a primeira terminar e varrer só essa, testando todos
os termos. A retomada desse plano usa só o inode. A proposta original de §4.4 fica registrada
acima.

### 4.5 A v1

**[decisão aceita]** Proposta: `MOOB_IOC_QUERY` (v1) sobre a raiz continua existindo, com a semântica
atual, mas só para quem tem `CAP_SYS_ADMIN` no namespace de usuário inicial (ferramentas de
administração e comparação com o oráculo). Para os demais devolve `EPERM`, porque percorre todos
os domínios e mantém o canal de tempo. Alternativa: manter a v1 aberta e documentar o canal.

## 5. Renomeação entre domínios

Arquivo renomeado para outro domínio: suas entradas mudam de partição na mesma chamada, e a regra
de homogeneidade é reavaliada no destino. **Diretório** renomeado para outro domínio: devolve
`EXDEV`, como entre filesystems; `mv` passa a copiar e apagar. **[decisão aceita]** Evita mover
subárvores inteiras de partição dentro de um `rename`.

## 6. Ferramentas

- `moobfs-index --domain DIR`: oráculo que avalia a mesma regra de homogeneidade e responde só
  sobre o domínio, para comparação.
- `moobfs-check`: lista arquivos heterogêneos por domínio.
- `bench`: forma W3 por domínio e o experimento do canal de tempo do registro 0030 como teste.

## 7. Testes exigidos

1. Entradas inválidas: versão, termos, marca `MISSING_OK` sozinha sem `order_by`, `order_by` em
   atributo inexistente ou de tipo errado, `cursor` corrompido, de outra consulta e após
   reavaliação (`ESTALE`), descritor que não é domínio (`ENOTTY`), `domain_depth=0`
   (`EOPNOTSUPP`), lote com mais de 8 consultas.
2. Equivalência com o oráculo por domínio, em todas as formas do W3, nos três inferiores.
3. Homogeneidade: `chmod` que torna um arquivo mais restrito o tira dos resultados na chamada
   seguinte; reverter o devolve; ACL estendida o exclui salvo se legível por todos; mudança no
   modo do diretório do domínio dispara reavaliação.
4. Canal de tempo: latência de uma consulta num domínio com 10 arquivos não varia (dentro do ruído)
   quando outro domínio vai de 0 a 20 000 arquivos que casam com o predicado.
5. Isolamento: usuário sem acesso ao diretório do domínio não consegue abri-lo nem consultar.
6. Renomeação: arquivo entre domínios muda de partição; diretório entre domínios devolve `EXDEV`.
7. Ordenação e cursor: "N mais recentes" igual ao oráculo ordenado; paginação completa por cursor
   devolve o mesmo conjunto que a consulta sem limite.
8. Concorrência: o teste de corrida do registro 0020 aplicado à v2, incluindo `chmod` no diretório
   do domínio durante consultas.
9. KASAN e lockdep sem diagnóstico; v1 restrita conforme 4.5.

## 8. Fases

| Fase | Conteúdo | Critério de pronto |
| --- | --- | --- |
| Q2a | Partições por domínio, regra de homogeneidade, `MOOB_IOC_QUERY2` sem `order_by`, restrição da v1 | Testes 1, 2, 3, 4, 5, 9 |
| Q2b | `order_by`, `cursor` | Teste 7 |
| Q2c | Interseção por leapfrog | Equivalência e medição contra Q2a na forma `namespace AND agent` |
| Q2d | Lote e `MISSING_OK` | Testes 1 e 2 para os novos campos |
| Q2e | Renomeação entre domínios | Teste 6 e 8 |

Cada fase entra por pull request próprio, com registro em `docs/history/`, a suíte da VM e o
KASAN, e medição no host da Azure comparada ao registro 0029.
