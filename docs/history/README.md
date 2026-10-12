# Memória do projeto

Este diretório guarda um diário técnico versionado. O Git preserva as alterações exatas dos arquivos; as ADRs explicam decisões e o diário conecta pedidos, mudanças e resultados.

## Onde encontrar cada informação

- `docs/decisions/`: decisões arquiteturais, alternativas e motivos resumidos.
- `docs/history/`: registros cronológicos de trabalho e evidências de validação.
- `CHANGELOG.md`: mudanças relevantes para quem usa/contribui.
- `docs/native-mvp.md`: proposta original do MVP completo.
- `AGENTS.md`: instruções de continuidade para próximas sessões.

Um registro deve conter data, objetivo, decisões/ADRs, arquivos ou commits relacionados, testes realmente executados, limitações e próximo passo. Use novos registros ou adendos datados para mudanças posteriores. Registros da sessão em andamento podem ser completados antes do commit.

Os registros iniciais reconstituem a conversa em forma resumida; não são uma transcrição literal ou exaustiva. Histórico de decisões registra o que foi decidido e suas justificativas técnicas, sem alegar acesso a pensamentos internos privados.

Preservação depende de commits e cópias: nenhum disco ou serviço garante retenção para sempre. Manter o repositório remoto e uma cópia independente permite recuperar o histórico. Saídas grandes, imagens de teste e binários ficam em `build/`; versionar os scripts que os reproduzem e resumos verificáveis dos resultados.

## Índice

- [0001 — Origem e definição do projeto](0001-origin.md)
- [0002 — Primeiro marco nativo](0002-native-read-milestone.md)
- [0003 — Base open source e GitHub](0003-open-source-github.md)
- [0004 — Memória e contexto de LLMs como objetivo explícito](0004-llm-memory-context.md)
- [0005 — Modelo de memória e o Be File System como referência](0005-bfs-reference-memory-model.md)
- [0006 — Publicação da ADR-0005 e levantamento do ecossistema livre](0006-ecosystem-survey.md)
- [0007 — Leitura técnica do ai-memory e posição relativa](0007-ai-memory-reading.md)
- [0008 — Infraestrutura do kernel reutilizável e especificação do M2 com escrita POSIX](0008-m2-spec-kernel-infra.md)
- [0009 — O que o MoobFS traria de diferente, e o princípio de não concorrer com o maduro](0009-differentiation-and-principle.md)
- [0010 — ADR-0007: camada empilhada sobre filesystem maduro; M2 reescrito](0010-adr-0007-stacked-layer.md)
- [0011 — Fase 2a: camada empilhada mínima sobre ext4, validada em VM](0011-phase-2a-stacked-passthrough.md)
- [0012 — Fase 2b: passagem completa sobre ext4, btrfs e XFS, com reflink delegado](0012-phase-2b-full-passthrough.md)
- [0013 — Fase 2c: atributos tipados validados no kernel, ferramentas e fuzzing](0013-phase-2c-typed-attributes.md)
- [0014 — Fase 2d: índice em memória e consulta por atributo (`QUERY`)](0014-phase-2d-index-query.md)
- [0015 — Fase 2e: aceitação com clientes reais e investigação de perda de linhas no SQLite](0015-phase-2e-investigation.md)
- [0016 — Harness com KASAN e lockdep em kernel próprio; diagrama de fluxo no site](0016-kasan-lockdep-harness.md)
- [0017 — Uso no macOS e postura de segurança comparada ao mercado](0017-macos-and-security-posture.md)
- [0018 — Reescrita da história do Git: remoção de perguntas e de dados pessoais](0018-history-rewrite.md)
- [0019 — Decisões a partir de uma revisão externa da arquitetura](0019-external-review-decisions.md)
- [0020 — Fase 3b: autorização POSIX no `QUERY`](0020-phase-3b-query-authorization.md)
- [0021 — Auditoria de anterioridade e estratégia de distribuição](0021-originality-and-distribution.md)
- [0022 — Avaliação de utilidade, proposta de valor e maturidade como critério de adoção](0022-value-assessment.md)
- [0023 — Revisão de implementação, planos e próximo experimento](0023-project-review.md)
- [0024 — Preparação para contribuições externas: políticas, templates, documentação e CI](0024-community-readiness.md)
- [0025 — M4: primeira fatia do harness de benchmark](0025-m4-harness-first-slice.md)
- [0026 — Primeira tag de pre-release: v0.1.0-experimental](0026-first-prerelease-tag.md)
- [0027 — `QUERY`: busca a partir do primeiro candidato e resultados copiados um a um](0027-query-fast-path.md)
- [0028 — Perfil do `QUERY` e revalidação de dentry compatível com RCU-walk](0028-rcu-walk-revalidation.md)
- [0029 — Série de escala do `QUERY` (1000 a 60 000 memórias)](0029-query-scale-series.md)
- [0030 — Verificações: fluxo de mudanças, Landlock e canal de tempo do `QUERY`](0030-verification-notify-landlock-timing.md)
- [0031 — Tempo de remontagem com 25 000 a 60 000 memórias](0031-remount-time.md)
- [0032 — Experiência do desenvolvedor no GitHub: plano e lote 1](0032-dx-batch1.md)
- [0033 — Pesquisas para a decisão sobre o papel do `QUERY` (ADR-0009)](0033-query-design-research.md)
- [0034 — ADR-0009 aceita: `QUERY` por domínio no kernel (alternativa E)](0034-adr-0009-accepted.md)
- [0035 — Fase Q2a: `QUERY` por domínio (UAPI v2), partições e regra de homogeneidade](0035-phase-q2a-domain-query.md)
- [0036 — Fase Q2b: ordenação e cursor no `QUERY2`](0036-phase-q2b-order-cursor.md)
- [0037 — Fase Q2c: interseção por avanço alternado no `QUERY2`](0037-phase-q2c-leapfrog.md)
- [0038 — Fase Q2d: termos "ausente ou ..." e lote no `QUERY2`](0038-phase-q2d-missing-batch.md)
- [0039 — Experiência do desenvolvedor no GitHub: lote 2 (ferramentas)](0039-dx-batch2.md)
- [0041 — Experiência do desenvolvedor no GitHub: lote 3 (vitrine)](0041-dx-batch3.md)
- [0042 — Novo host de medição: Mac mini Server 2011 com Debian 13](0042-bench-host-macmini.md)
- [0043 — Canal de tempo: `QUERY2` por domínio contra a v1 (teste 4 da especificação)](0043-timing-channel-query2.md)
- [0044 — `QUERY2` medido contra a v1 e revisão do plano de igualdade da Q2c](0044-q2-measured-equality-plan.md)
- [0045 — Triagem dos 30 alertas abertos do CodeQL](0045-codeql-triage.md)
- [0046 — Dependências Python do workflow KASAN fixadas por hash](0046-kasan-pip-hashes.md)
- [0047 — `QUERY2` contra o SQLite no mesmo hardware](0047-sqlite-baseline-macmini.md)
- [0048 — Otimização do "N mais recentes" no `QUERY2`](0048-recent-optimization.md)
- [0049 — `QUERY2`, v1 e SQLite em btrfs e XFS](0049-q2-btrfs-xfs.md)
- [0050 — Pendências de código recebidas do lote de experiência do desenvolvedor](0050-dx-code-items.md)
