# 0046 — Dependências Python do workflow KASAN fixadas por hash

Data: 2026-10-12. Branch: `ci/pin-virtme-ng`, a partir de `origin/main` (`1b87764`). Estado:
implementado; a primeira execução real com hashes é a do workflow KASAN neste PR.

## Objetivo

Resolver o único achado de Pinned-Dependencies do OpenSSF Scorecard (alerta 1, nota 8): em
`.github/workflows/kasan.yml`, o `pip install` fixava só a versão do `virtme-ng`
(`virtme-ng==1.41`), e as dependências dele ficavam sem versão e sem hash. Numeração: 0046,
primeiro número livre segundo a sessão da fila Q2 (que usa 0035 a 0038 e 0042 a 0044); conferidas
`origin/main` e as branches remotas.

## Decisão

Instalar a partir de `vm/kasan-requirements.txt`, gerado por `pip-compile --generate-hashes`
(pip-tools 7.4.1, Python 3.12, a versão do Ubuntu 24.04 dos runners), com
`pip install --require-hashes`. O arquivo fixa o `virtme-ng` 1.41 e as seis dependências
resolvidas (`argcomplete` 3.7.2, `certifi` 2026.7.22, `charset-normalizer` 3.5.2, `idna` 3.20,
`requests` 2.34.2, `urllib3` 2.8.0), com os hashes de todas as distribuições publicadas, e traz
no cabeçalho o comando para regenerá-lo. A variável `VIRTME_NG_VERSION` saiu do workflow, para
que a versão tenha uma única fonte.

O arquivo fica em `vm/` porque é do harness. Isso não muda nada no gatilho: `vm/**` e o próprio
`kasan.yml` já disparam o KASAN.

## Mudanças

- Novo `vm/kasan-requirements.txt` (com SPDX).
- `.github/workflows/kasan.yml`: `pip install --user --require-hashes -r
  vm/kasan-requirements.txt`; sai `VIRTME_NG_VERSION`.
- `docs/development.md`: o passo do KASAN local usa o mesmo arquivo.
- `CHANGELOG.md` e `docs/history/README.md`.

Não foi tocado nenhum arquivo da fila Q2 (`vm/suite.sh`, `bench/load.c`), como a sessão da Q2
pediu.

## Evidências

- `pip-compile` num contêiner `python:3.12-slim` (amd64) gerou o arquivo.
- Num contêiner `ubuntu:24.04` amd64 com o `python3-pip` do apt,
  `python3 -m pip install --user --require-hashes -r vm/kasan-requirements.txt` instalou as sete
  distribuições e `vng --version` respondeu `virtme-ng 1.41`. O contêiner precisou de
  `PIP_BREAK_SYSTEM_PACKAGES=1` (PEP 668); o runner do GitHub já aceitava o `--user` sem isso,
  como mostram as execuções anteriores do workflow. Se o runner passar a recusar, a correção é a
  mesma variável no passo.
- O build do kernel e a suíte sob KASAN não foram executados localmente; rodam no PR.

## Limitações

- As versões das dependências ficam congeladas; uma atualização exige regenerar o arquivo.
  O Dependabot não acompanha este arquivo, porque fica fora de um diretório configurado para
  `pip`.

## Próximo passo

Conferir o job KASAN do PR e, depois do merge, a próxima nota do Scorecard em
Pinned-Dependencies.
