# M4 campaign on an accelerated guest: runbook

Status: proposal (2026-10-10), not executed. Complements `docs/benchmark-plan.md` ("Method":
dedicated VM with KVM; emulation only for correctness) and `docs/benchmark-harness.md`. The harness
is complete and validated in the emulated VM; this document says how to obtain real numbers.

## 1. Why the current VM is no good for measuring

`make vm-test` runs an x86_64 kernel (Debian 6.12.111) in QEMU inside Docker Desktop. On an
Apple Silicon Mac this is instruction-by-instruction emulation (TCG), tens of times slower
than the hardware and with a latency distribution that represents nothing. The GitHub runner
also has no KVM. Therefore, no time printed by these runs is a measurement of the project.

## 2. Three paths

| Path | Hardware | Guest architecture | Value of the numbers | Cost |
| --- | --- | --- | --- | --- |
| A. Linux x86_64 host with KVM (bare metal or cloud with nested virtualization) | any | x86_64, the same Debian 6.12.111 kernel already tested | reference for the M4 report | rented hours or own machine |
| B. Apple Silicon Mac with Hypervisor.framework (Lima, UTM or `qemu-system-aarch64 -accel hvf`) | the development Mac | arm64, Debian 13 with the 6.12 arm64 kernel | relative A/B/C comparison on the same machine; architecture outside the tested matrix | zero |
| C. x86_64 cloud VM used as the host (Azure D4as_v5, Oracle E3.Flex or similar), with the module in the instance's own kernel | any | x86_64, kernel 6.12.112 built on the instance by `bench/kernel-build.sh` | measurement on a cloud VM, a realistic production environment; same tarball as KASAN, without instrumentation | rented hours |

Recommendation (2026-10-11): C is the path chosen for the first campaign, since it combines x86,
low cost and a second kernel in the matrix; A remains the reference when there is
dedicated hardware; B stays as an optional preliminary. In B, the module is built for a new architecture: before measuring, the whole
suite (`vm/suite.sh`) and a run under KASAN must pass on that kernel, and the report
must say "virtualized arm64, outside the matrix".

## 2.1 Path C: cloud VM as the host (Azure)

Creation (Azure portal): Ubuntu Server 24.04 LTS x64 Gen2 image; size
`Standard_D4as_v5` (4 vCPU, 16 GB) or `D4s_v5`; **security type Standard**, not Trusted
launch, because the built kernel is not signed and Secure Boot would refuse it; SSH key; only
port 22; OS disk Premium SSD 64 GB; **three identical data disks** for ext4, btrfs and
XFS, with cache "None"; no B series (burstable); auto-shutdown.

Disks for the campaign: **Premium SSD v2** with fixed IOPS and throughput, equal on all three. Premium
SSDs from P1 to P20 have credit-based bursting on by default (the P6 guarantees 240 IOPS and rises up to
3500 while there are credits, which run out in about 30 minutes of load), so the device speed
would change in the middle of a campaign of hours and the difference would be attributed to the measured
system. P30 and above have no credit-based bursting, but are large and expensive for this use. P6
disks with bursting serve for the `HOST_SETUP_PASS` gate and for the pilot, never for the report.

On the instance, as root, in order:

```bash
apt-get update && apt-get install -y build-essential flex bison libelf-dev libssl-dev bc dpkg-dev debhelper rsync kmod git
git clone https://github.com/studiomoob/llmoobfs && cd llmoobfs
sh bench/kernel-build.sh            # 6.12.112 from the Ubuntu config + vm/bench.config; 45-60 min on 4 vCPU
sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/' /etc/default/grub && update-grub
grep -E "menuentry '|submenu '" /boot/grub/grub.cfg | cut -d"'" -f2     # exact name of the 6.12 entry
grub-reboot "Advanced options for Ubuntu>Ubuntu, with Linux 6.12.112-moobfs-bench"   # next boot only
shutdown -h now   # in the portal: VM deallocated, turn off "secure boot", start
uname -r          # 6.12.112-moobfs-bench; if it does not come up, rebooting goes back to the Azure kernel
lsblk                               # identify the three data disks (sdc, sdd, sde or similar)
sh bench/host-setup.sh --ext4 /dev/sdc --btrfs /dev/sdd --xfs /dev/sde   # formats, builds, runs the whole suite
```

`HOST_SETUP_PASS` is the gate: the full suite (`MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`)
passed on that kernel and on those disks, with no diagnostics in `dmesg`. Any failure becomes a journal
before measuring. Then, calibration (§5) and campaign (§6) with the same devices mounted.
If KASAN for kernel 6.12.112 has not yet run on that machine, it is not necessary: the
workflow already covers that kernel; what the suite validates here is the host and the disks.

## 3. Prepare the guest (path B, Lima)

Everything below is the proposed procedure; the commands were not executed in this repository.

```bash
brew install lima
limactl create --name moob --vm-type vz --cpus 4 --memory 8 --disk 40 template://debian-13
limactl start moob
limactl shell moob
```

Inside the guest (Debian arm64):

```bash
sudo apt-get update && sudo apt-get install -y build-essential linux-headers-$(uname -r) \
  e2fsprogs btrfs-progs xfsprogs sqlite3 libsqlite3-dev git dmsetup acl python3 fio
git clone https://github.com/studiomoob/llmoobfs && cd llmoobfs
make layer KDIR=/lib/modules/$(uname -r)/build
make attr-tools bench-tools BUILD=build/arm64 CFLAGS="-O2 -g"
gcc -O2 -Wall -Wextra -Werror -Iinclude tests/query.c common/attr.c -o build/arm64/query
gcc -O2 -Wall -Wextra -Werror tests/layer.c -o build/arm64/layer
gcc -O2 -Wall -Wextra -Werror tests/idmap.c -o build/arm64/idmap
gcc -O2 -Itests/third_party/fsx tests/third_party/fsx/fsx.c -o build/arm64/fsx
gcc -O2 tests/third_party/log-writes/replay-log.c tests/third_party/log-writes/log-writes.c -o build/arm64/replay-log
cp layer/moobfs_layer.ko bench/queries-w3.txt build/arm64/
for f in sqlbase equiv run; do cp bench/$f.sh build/arm64/bench-$f.sh; done
cp bench/queries-w3.txt build/arm64/bench-queries-w3.txt
```

Dedicated disks: in Lima, add virtio disks with `limactl disk create` and `--mount`,
or, for the preliminary, use image files on loop (worse for disk numbers, acceptable
for calibrating):

```bash
sudo truncate -s 2G /var/tmp/ext4.img /var/tmp/btrfs.img /var/tmp/xfs.img
sudo truncate -s 256M /var/tmp/scratch.img /var/tmp/log.img
sudo mke2fs -q -F -t ext4 -b 4096 -L moob-lower /var/tmp/ext4.img
sudo mkfs.btrfs -q -f -L moob-lower /var/tmp/btrfs.img
sudo mkfs.xfs -q -f -L moob-lower /var/tmp/xfs.img
for i in ext4 btrfs xfs scratch log; do sudo losetup -f --show /var/tmp/$i.img; done
```

## 4. Validate before measuring

The suite is parameterized as in `vm/kasan-guest.sh`: `BIN`, `TOOLS`, `MODS`, `LOWER`,
`LAYER` and `DEV_*`. As root in the guest:

```bash
export BIN=$PWD/build/arm64 TOOLS=/usr/bin MODS=/nonexistent LOWER=/mnt/moob-lower LAYER=/mnt/moob-layer
export DEV_EXT4=/dev/loop0 DEV_BTRFS=/dev/loop1 DEV_XFS=/dev/loop2 DEV_SCRATCH=/dev/loop3 DEV_LOG=/dev/loop4
mkdir -p $LOWER $LAYER; ln -sf /usr/sbin/dmsetup /usr/sbin/mke2fs /usr/sbin/e2fsck /usr/bin/setfacl /usr/bin/getfacl $BIN/ 2>/dev/null
insmod build/arm64/moobfs_layer.ko
sh vm/suite.sh | tee build/arm64/suite.log
```

Criterion: `MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS`, with no `WARNING`, `BUG` or `Oops` in
`dmesg`. Likely architecture differences the suite will expose: page size (Lima
uses 4 KiB on Debian arm64; 16 KiB would change ext4 xattr limits), and a missing `/tools`
(`TOOLS` points to `/usr/bin`). Any failure becomes its own journal before any number.

## 5. Calibrate the disk

`sh bench/calibrate.sh --out <dir da campanha> ext4=/srv/moob/ext4 btrfs=/srv/moob/btrfs
xfs=/srv/moob/xfs`: 30 s of random 4 KiB writes, each followed by `fsync`, per
lower filesystem; prints operations per second and the latency of the `fsync` itself (p50, p99) and saves the
`fio` JSON. The write completion latency (`clat`) is no good for this: with `--fsync=1`
it measures only the write into the page cache (about 10 µs on the Azure host). The summary goes into the
report together with `environment.txt`.

## 6. Run the campaign

Mount the layer per lower filesystem and call the orchestrator with the three systems:

```bash
mount /dev/loop0 /mnt/a-ext4 && mount /dev/loop1 /mnt/b-btrfs
mount /dev/loop2 /mnt/c-lower && mount -t moobfs_layer -o lower=/mnt/c-lower none /mnt/c-layer
BIN=$PWD/build/arm64 sh bench/run.sh --out build/bench/$(date -u +%Y%m%dT%H%M)-arm64 \
  --scale 1000 --profile 8 --reps 7 --procs 4 \
  --plain A-ext4=/mnt/a-ext4/bench --plain B-btrfs=/mnt/b-btrfs/bench \
  --layer C-ext4=/mnt/c-layer:/mnt/c-lower
```

Scales S1 to S6 and profiles 6, 8 and 16 are separate runs of the same command. `report.py`
produces `results.csv` and marks "inconclusive" above 20% dispersion. Index saturation: the
same campaign with a reduced `index_budget=` at mount, reading `get_info.txt` of each system.

## 7. What to record

Per campaign: `environment.txt` (`run.sh` records kernel, CPU, memory, presence of KVM or
HVF, SQLite version, commit), the `fio` calibration, the output of `equiv.txt` with the justified
`SCAN` plans, and `results.csv`. The report in `docs/history/` cites the path of each
file and declares the architecture and the acceleration type. No number goes into the README, site or
value document without that path.
