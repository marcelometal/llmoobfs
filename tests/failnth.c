/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * failnth: systematic allocation-failure injection into the layer, one system call at a time
 * (journal 0096; the first query on a domain, which builds its index, since journal 0098). For each operation it asks the kernel to fail the 1st, then the 2nd, then the
 * N-th allocation made on behalf of this thread, through /proc/thread-self/fail-nth, until an
 * attempt runs with no fault injected. The caller (vm/kasan-guest.sh) confines the faults to
 * allocations with the module on the stack (failslab and fail_page_alloc, require-start and
 * require-end) and afterwards checks the kernel log and compares the index with the oracle.
 * The method is the kernel's own: Documentation/fault-injection/fault-injection.rst,
 * "Systematic faults using fail-nth".
 *
 *   failnth <layer mount with domain_depth=1> <lower directory>
 *
 * Here a failed call must fail cleanly: an error return, never a signal, and never success with
 * a different result. Prints one line per operation and FAILNTH_DONE with the totals; exits 1
 * if any call returned an error that an allocation failure does not explain.
 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <linux/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

static char mnt[512], low[512];
static int nth_fd;
static unsigned long total_faults, total_calls, unexpected;
static int seq;

static const char *up(const char *rel)
{
	static char buf[4][1024];
	static int i;

	i = (i + 1) % 4;
	snprintf(buf[i], sizeof buf[i], "%s/%s", mnt, rel);
	return buf[i];
}

static void arm(int n)
{
	char b[16];
	int len = snprintf(b, sizeof b, "%d", n);

	if (pwrite(nth_fd, b, (size_t)len, 0) != len) { perror("fail-nth"); exit(2); }
}

/* 1 when the armed fault was injected (the counter reached zero); disarms in any case */
static int fired(void)
{
	char b[16] = "";
	int r;

	if (pread(nth_fd, b, sizeof b - 1, 0) < 0) { perror("fail-nth read"); exit(2); }
	r = atoi(b) == 0;
	arm(0);
	return r;
}

/* errors an allocation failure can turn into; anything else is reported */
static int explained(int e)
{
	return e == ENOMEM || e == ENOSPC || e == EIO || e == ENOBUFS || e == EAGAIN || e == EINTR;
}

static void fresh(char *rel, size_t cap, const char *dom, int with_attr)
{
	char v[8] = { MOOB_T_STRING, 'n', 'o', 't', 'e' };
	int fd;

	snprintf(rel, cap, "%s/f%06d", dom, seq++);
	fd = open(up(rel), O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) { perror("fresh"); exit(2); }
	if (write(fd, "x", 1) != 1) { perror("fresh write"); exit(2); }
	if (with_attr && fsetxattr(fd, "user.moob.kind", v, 5, 0)) { perror("fresh attr"); exit(2); }
	close(fd);
}

static void q2_fill(struct moob_query2 *q, struct moob_query_result *res, __u32 limit)
{
	memset(q, 0, sizeof *q);
	q->version = MOOB_QUERY2_VERSION;
	q->nterms = 1;
	q->limit = limit;
	q->results = (__u64)(unsigned long)res;
	snprintf(q->terms[0].name, sizeof q->terms[0].name, "kind");
	q->terms[0].op = MOOB_OP_EQ;
	q->terms[0].type = MOOB_T_STRING;
	q->terms[0].len = 4;
	memcpy(q->terms[0].value, "note", 4);
}

enum { OP_SET_NEW, OP_SET_REPLACE, OP_SET_RAW_OVER, OP_REMOVE, OP_CREATE, OP_MKDIR, OP_RENAME_OVER,
       OP_UNLINK, OP_QUERY2, OP_QUERY2_ORDER, OP_BATCH, OP_INFO, OP_CHMOD_DIR, OP_READ, OP_LOOKUP,
       OP_FIEMAP, OP_SYNCFS, OP_BUILD, OP_COUNT };
static const char *const names[OP_COUNT] = {
	"setxattr-new", "setxattr-replace", "setxattr-raw-over-typed", "removexattr", "create",
	"mkdir", "rename-over-indexed", "unlink-indexed", "query2", "query2-ordered", "query2-batch",
	"get-info", "chmod-directory", "open-read", "lookup-cold", "fiemap", "syncfs",
	"query2-first-on-a-domain",
};

int main(int argc, char **argv)
{
	static struct moob_query_result res[64];
	char path[64];
	int op;

	if (argc != 3) { fprintf(stderr, "usage: failnth <layer mount> <lower directory>\n"); return 64; }
	snprintf(mnt, sizeof mnt, "%s", argv[1]);
	snprintf(low, sizeof low, "%s", argv[2]);
	snprintf(path, sizeof path, "/proc/self/task/%ld/fail-nth", (long)syscall(SYS_gettid));
	nth_fd = open(path, O_RDWR);
	if (nth_fd < 0) { perror(path); return 2; }
	mkdir(up("da"), 0755); mkdir(up("db"), 0755);
	/*
	 * The index of a domain is built by the first query on it (ADR-0012), and a write to a
	 * domain that is not built does not touch the index: build both, so that the write
	 * operations below fail inside the index updates.
	 */
	for (op = 0; op < 2; op++) {
		struct moob_query2 q0;
		int d0 = open(up(op ? "db" : "da"), O_RDONLY | O_DIRECTORY);

		q2_fill(&q0, res, 64);
		if (d0 < 0 || ioctl(d0, MOOB_IOC_QUERY2, &q0)) { perror("first query"); return 2; }
		close(d0);
	}

	for (op = 0; op < OP_COUNT; op++) {
		int n, faults = 0, clean_fail = 0, ok_despite = 0;

		for (n = 1; n < 400; n++) {
			char a[256], b[256], v[16] = { MOOB_T_STRING, 'f', 'a', 'c', 't' };
			char raw[4] = { MOOB_T_RAW, 1, 2, 3 };
			struct moob_query2 q[2];
			struct moob_query2_batch bt;
			struct moob_info info;
			struct fiemap fm;
			struct stat st;
			int fd = -1, dfd = -1, r = 0, e, hit;

			/* preparation, disarmed */
			switch (op) {
			case OP_SET_NEW: fresh(a, sizeof a, "da", 0); break;
			case OP_SET_REPLACE: case OP_SET_RAW_OVER: case OP_REMOVE: case OP_UNLINK:
			case OP_READ: case OP_FIEMAP: fresh(a, sizeof a, "da", 1); break;
			case OP_RENAME_OVER: fresh(a, sizeof a, "da", 1); fresh(b, sizeof b, "da", 1); break;
			case OP_CREATE: snprintf(a, sizeof a, "da/c%06d", seq++); break;
			case OP_MKDIR: snprintf(a, sizeof a, "da/d%06d", seq++); break;
			case OP_LOOKUP: fresh(a, sizeof a, "db", 1); break;
			case OP_BUILD:	/* a domain nobody has queried: the armed query builds it */
				snprintf(b, sizeof b, "e%06d", seq++);
				if (mkdir(up(b), 0755)) { perror("domain"); return 2; }
				fresh(a, sizeof a, b, 1); fresh(a, sizeof a, b, 1); fresh(a, sizeof a, b, 0);
				dfd = open(up(b), O_RDONLY | O_DIRECTORY);
				if (dfd < 0) { perror("domain"); return 2; }
				break;
			default: break;
			}
			if (op == OP_QUERY2 || op == OP_QUERY2_ORDER || op == OP_BATCH || op == OP_SYNCFS) {
				dfd = open(up("da"), O_RDONLY | O_DIRECTORY);
				if (dfd < 0) { perror("domain"); return 2; }
			}
			if (op == OP_INFO) { dfd = open(mnt, O_RDONLY | O_DIRECTORY); if (dfd < 0) { perror("root"); return 2; } }
			if (op == OP_FIEMAP) { fd = open(up(a), O_RDONLY); if (fd < 0) { perror("fiemap open"); return 2; } }

			arm(n);
			errno = 0;
			switch (op) {
			case OP_SET_NEW: case OP_SET_REPLACE:
				r = setxattr(up(a), "user.moob.kind", v, 5, 0); break;
			case OP_SET_RAW_OVER:
				r = setxattr(up(a), "user.moob.kind", raw, 4, 0); break;
			case OP_REMOVE:
				r = removexattr(up(a), "user.moob.kind"); break;
			case OP_CREATE:
				r = open(up(a), O_WRONLY | O_CREAT | O_EXCL, 0644); if (r >= 0) { close(r); r = 0; } break;
			case OP_MKDIR:
				r = mkdir(up(a), 0755); break;
			case OP_RENAME_OVER:
				r = rename(up(a), up(b)); break;
			case OP_UNLINK:
				r = unlink(up(a)); break;
			case OP_QUERY2: case OP_BUILD:
				q2_fill(&q[0], res, 64); r = ioctl(dfd, MOOB_IOC_QUERY2, &q[0]); break;
			case OP_QUERY2_ORDER:
				q2_fill(&q[0], res, 20);
				snprintf(q[0].order_by, sizeof q[0].order_by, "kind");
				q[0].order = MOOB_ORDER_DESC; q[0].order_type = MOOB_T_STRING;
				r = ioctl(dfd, MOOB_IOC_QUERY2, &q[0]); break;
			case OP_BATCH:
				q2_fill(&q[0], res, 32); q2_fill(&q[1], res + 32, 32);
				memset(&bt, 0, sizeof bt);
				bt.version = MOOB_QUERY2_VERSION; bt.nqueries = 2; bt.queries = (__u64)(unsigned long)q;
				r = ioctl(dfd, MOOB_IOC_QUERY2_BATCH, &bt); break;
			case OP_INFO:
				r = ioctl(dfd, MOOB_IOC_GET_INFO, &info); break;
			case OP_CHMOD_DIR:
				r = chmod(up("db"), (n & 1) ? 0750 : 0755); break;
			case OP_READ:
				r = open(up(a), O_RDONLY); if (r >= 0) { char c; ssize_t k = read(r, &c, 1); close(r); r = k == 1 ? 0 : -1; } break;
			case OP_LOOKUP:
				r = stat(up(a), &st); break;
			case OP_FIEMAP:
				memset(&fm, 0, sizeof fm); fm.fm_length = ~0ULL;
				r = ioctl(fd, FS_IOC_FIEMAP, &fm); break;
			case OP_SYNCFS:
				r = syncfs(dfd); break;
			}
			e = errno;
			hit = fired();
			if (fd >= 0) close(fd);
			if (dfd >= 0) close(dfd);
			total_calls++;
			if (!hit) {
				if (r) { printf("  %s: failed with no fault injected: %s\n", names[op], strerror(e)); unexpected++; }
				break;
			}
			faults++; total_faults++;
			if (r) {
				if (explained(e)) clean_fail++;
				else { printf("  %s: fault %d gave an unexplained error: %s\n", names[op], n, strerror(e)); unexpected++; }
			} else {
				ok_despite++;	/* the failed allocation was optional, retried or handled */
			}
		}
		printf("FAILNTH %-26s faults=%d failed_cleanly=%d succeeded_anyway=%d\n", names[op], faults, clean_fail, ok_despite);
		fflush(stdout);
	}
	printf("FAILNTH_DONE: operations=%d calls=%lu faults=%lu unexpected=%lu\n", OP_COUNT, total_calls, total_faults, unexpected);
	return unexpected ? 1 : 0;
}
