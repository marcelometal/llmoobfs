CC ?= cc
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow
COMMON = common/hash.c common/format.c
HEADERS = include/moobfs_format.h
KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD ?= build/host

.PHONY: all help tools attr-tools bench-tools bench-smoke hostile-test test fuzz lint checkpatch module layer vm-test
all: tools
help:
	@echo 'Host targets (macOS or Linux; never load a module on the host):'
	@echo '  make / make tools  build mkfs.moobfs, fsck.moobfs and attr_test into $$(BUILD)'
	@echo '  make test          attr_test unit tests and malformed-image tests'
	@echo '  make lint          shellcheck on vm/ and scripts/ shell scripts, SPDX headers on C sources (same as CI)'
	@echo '  make checkpatch    kernel checkpatch.pl --no-tree on layer/ and kernel/ (required in CI; needs curl, perl)'
	@echo '  make fuzz          libFuzzer over the attribute encoding (LLVM clang with libFuzzer)'
	@echo 'Linux-only targets:'
	@echo '  make attr-tools    moobfs-attr, moobfs-check, moobfs-index'
	@echo '  make bench-tools   M4 harness binaries; make bench-smoke runs its smoke test'
	@echo '  make hostile-test  adversarial lower trees through moobfs-check and moobfs-index (S4)'
	@echo 'Kernel (inside the VM container, not on your host):'
	@echo '  make vm-test       full native suite in a disposable QEMU VM; needs only Docker'
	@echo '  make module/layer  build the kernel modules against KDIR'
	@echo 'Details: docs/development.md and docs/testing.md'
tools: $(BUILD)/mkfs.moobfs $(BUILD)/fsck.moobfs $(BUILD)/attr_test
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/mkfs.moobfs: tools/mkfs.c $(COMMON) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/mkfs.c $(COMMON) $(LDFLAGS) -o $@
$(BUILD)/fsck.moobfs: tools/fsck.c $(COMMON) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/fsck.c $(COMMON) $(LDFLAGS) -o $@
$(BUILD)/attr_test: tests/attr_test.c common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tests/attr_test.c common/attr.c $(LDFLAGS) -o $@
# Linux-only tools over the xattr API (built statically inside the VM container too).
attr-tools: $(BUILD)/moobfs-attr $(BUILD)/moobfs-check $(BUILD)/moobfs-index
$(BUILD)/moobfs-attr: tools/moobfs-attr.c common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-attr.c common/attr.c $(LDFLAGS) -o $@
$(BUILD)/moobfs-check: tools/moobfs-check.c common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-check.c common/attr.c $(LDFLAGS) -o $@
$(BUILD)/moobfs-index: tools/moobfs-index.c tools/query-common.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-index.c common/attr.c $(LDFLAGS) -o $@
# M4 benchmark harness (docs/benchmark-harness.md). gen is portable; load is Linux-only.
bench-tools: $(BUILD)/bench-gen $(BUILD)/bench-load $(BUILD)/bench-sqlclient $(BUILD)/bench-aimem-latency $(BUILD)/bench-readcost
$(BUILD)/bench-gen: bench/gen.c bench/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) bench/gen.c -lm $(LDFLAGS) -o $@
$(BUILD)/bench-load: bench/load.c bench/common.h tools/query-common.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude -Itools bench/load.c common/attr.c -lm $(LDFLAGS) -o $@
# the SQLite client needs libsqlite3-dev; SQLITE_LIBS adds what a static link requires
SQLITE_LIBS ?= -lsqlite3
$(BUILD)/bench-sqlclient: bench/sqlclient.c bench/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) bench/sqlclient.c $(SQLITE_LIBS) -lm $(LDFLAGS) -o $@
# what a client pays to read a query's results, for the READ_BATCH decision (docs/spec-m3.md §4)
$(BUILD)/bench-readcost: bench/readcost.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude bench/readcost.c $(LDFLAGS) -o $@
# stage 2 of the real-client test (docs/client-test-plan.md): SQLite and QUERY2 in one process
$(BUILD)/bench-aimem-latency: bench/client/aimem-latency.c bench/common.h include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude bench/client/aimem-latency.c $(SQLITE_LIBS) -lm $(LDFLAGS) -o $@
bench-smoke: bench-tools attr-tools
	sh bench/smoke.sh $(BUILD)
# Security roadmap S4, user-space line: trees no well-behaved writer would leave on the lower.
$(BUILD)/hostile: tests/hostile.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror tests/hostile.c $(LDFLAGS) -o $@
hostile-test: attr-tools $(BUILD)/hostile
	sh tests/hostile-user.sh $(BUILD) $(HOSTILE_SEEDS)
test: tools
	$(BUILD)/attr_test
	MOOB_BUILD_DIR=$(BUILD) python3 tests/test_images.py
# libFuzzer run over the attribute encoding (needs LLVM clang with libFuzzer, e.g. on Linux).
FUZZ_TIME ?= 20
fuzz: | $(BUILD)
	clang -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude tests/fuzz_attr.c common/attr.c -o $(BUILD)/fuzz_attr
	mkdir -p $(BUILD)/fuzz-corpus
	$(BUILD)/fuzz_attr -max_total_time=$(FUZZ_TIME) -max_len=4200 tests/fuzz-corpus/attr $(BUILD)/fuzz-corpus
lint:
	@command -v shellcheck >/dev/null 2>&1 || { echo 'shellcheck not found: apt install shellcheck, or brew install shellcheck'; exit 1; }
	shellcheck --shell=sh --severity=warning $$(git ls-files 'vm/*.sh' 'scripts/*.sh')
	sh scripts/check-spdx.sh
# Style check, required in CI since journal 0050; the script pins and verifies checkpatch.pl from a stable kernel tag.
checkpatch:
	sh scripts/checkpatch.sh
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules
layer:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/layer modules
vm-test:
	sh vm/run.sh
