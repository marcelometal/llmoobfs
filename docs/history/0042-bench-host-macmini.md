# 0042 — Novo host de medição: Mac mini Server 2011 com Debian 13

Data: 2026-10-11 e 2026-10-12. Estado: host preparado e validado com a suíte completa; nenhuma
medição de desempenho ainda.

## Contexto

A VM da Azure usada nos registros 0026 a 0031 foi desligada pelo mantenedor. O novo host é uma
máquina local do mantenedor. Endereço, usuário e chave não são registrados aqui.

## Hardware e sistema

| Item | Valor |
| --- | --- |
| Máquina | Mac mini Server Mid 2011 |
| CPU | Intel Core i7-2635QM, 4 núcleos, 8 threads, 2,0 GHz |
| Memória | 16 GB DDR3 1333 |
| Disco do sistema | HD de 500 GB, 2,5", rotacional (Seagate ST9500420ASG) |
| Disco dos testes | SSD OCZ Vertex 4 de 128 GB, SATA 6 Gb/s; SMART: aprovado, vida restante 92%, 0 setores realocados |
| Sistema | Debian 13.7, kernel padrão `6.12.111+deb13-amd64` (o mesmo da VM da suíte) |
| Volumes de teste | três partições GPT de 20 GB no SSD: ext4, btrfs, XFS, formatadas por `bench/host-setup.sh` |

Diferenças em relação à Azure: outra CPU (Sandy Bridge, 2011) e discos locais (SSD SATA em vez de
disco gerenciado P6). Números absolutos não são comparáveis com os registros 0026 a 0031; as
comparações da série Q2 serão feitas dentro deste host, incluindo uma nova medição de base da v1.
Sem kernel próprio: o kernel padrão da Debian já é da série 6.12 validada.

## Preparação

1. O SSD tinha macOS (APFS, 64 GB) e uma partição NTFS (55 GB). Antes de apagá-lo, com
   autorização do mantenedor, foi feita uma imagem completa comprimida no HD (`zstd -3`):
   128 GB lidos em 902 s, imagem de 23,8 GB, verificada pela igualdade entre o SHA-256 do disco
   lido e o da imagem descomprimida (verificação total de 1 592 s). Instruções de restauração
   ficam ao lado da imagem, no próprio host.
2. O relógio do host estava cerca de 49 minutos adiantado e sem NTP; `systemd-timesyncd` foi
   instalado e a sincronização ligada antes de qualquer medição.
3. `sudo` e os headers do kernel (`linux-headers-$(uname -r)`) não vinham na instalação mínima;
   foram instalados. O `bench/host-setup.sh` não instala os headers quando o kernel é o da
   distribuição; fica como melhoria.
4. Primeira execução de `bench/host-setup.sh`: falhou antes de compilar, porque o script gravava
   o log de compilação em `build/` sem criar o diretório (cópia nova da árvore, sem `build/`).
   Corrigido com `mkdir -p` (commit na branch da Q2d).

## Evidência

Segunda execução de `bench/host-setup.sh`, com a árvore da branch `m3/query-v2-q2d` (Q2a a Q2d):
`HOST_SETUP_PASS` no kernel `6.12.111+deb13-amd64`, com `QUERY2_ORDER_PASS`,
`QUERY2_MISSING_BATCH_PASS`, `QUERY2_LEAPFROG_PASS`, `QUERY2_PASS`, `ISOLATION_PASS`, `EQUIV_PASS`,
`BENCH_LAYER_PASS` e `LAYER_LOWER_PASS` em ext4, btrfs e XFS, `MOOBFS_ACCEPTANCE_PASS` (git,
SQLite WAL, cliente de memória, replay de `dm-log-writes`, `dm-flakey`) e `MOOBFS_LAYER_PASS`;
nenhuma linha `BUG:`, `Oops:` ou `WARNING:` no `dmesg`. É a primeira execução da série Q2 em
hardware real, fora de VM.

## Próximo passo

Medições neste host: base da v1 (W3 da série do registro 0029), W3 por domínio com a Q2,
"N mais recentes", interseção (Q2c contra Q2b, módulos alternados) e o teste do canal de tempo
(`bench/timing-v2.sh`).
