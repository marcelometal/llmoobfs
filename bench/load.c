/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bench/load: replays a manifest against a target directory and records the
 * latency of every call (docs/benchmark-harness.md §2, §5). Linux only.
 *
 *   load --manifest FILE --target DIR --workload W1|W2|W3|W4 --out PREFIX
 *        [--procs N] [--queries FILE] [--root DIR]
 *
 * W1 creates each memory: write <path>.tmp, set every attribute, fsync, rename
 *    to <path>, fsync the parent directory (the client convention of spec-m3).
 * W2 opens and reads every memory by its path.
 * W3 issues each line of --queries (name op type value, up to 4 terms) as one
 *    MOOB_IOC_QUERY on --root (default: --target), limit 4096, --repeat times per line;
 *    result = count, or -EOVERFLOW when the answer was truncated. Query records carry the
 *    line number as the shape (BENCH_QUERY_OP). --uid N runs as that user, to measure the
 *    per-result authorization of a regular caller. --query2 1 issues MOOB_IOC_QUERY2 instead,
 *    with --root naming a domain directory (docs/spec-query-v2.md); --order NAME:TYPE:asc|desc
 *    and --limit N give the ordered "N most recent" shape.
 * W4 rewrites "score" and then sets and removes a "scratch" attribute per memory.
 * W5 (needs --queries and --root): processes 1..N-1 walk their share of the manifest renaming
 *    each memory away and back, then unlinking and recreating it with its attributes, while
 *    process 0 cycles through the query file until every mutator has finished. Records: RENAME
 *    and RECREATE for the mutators, QUERY for the reader; a query may legitimately see fewer
 *    results during the churn, so result counts are informative only.
 * W8 mixed load: each process walks its share of the manifest and, per memory, picks with a
 *    seeded PRNG a read (70%), an attribute update (20%) or one QUERY from --queries (10%,
 *    only when --queries and --root are given); records carry workload 8 and the op code.
 * W6/W7 (root): unmounts --target if mounted, drops the page cache for W6, mounts
 *    moobfs_layer with lower=--lower on --target and issues the first line of
 *    --queries; the record covers mount() plus that QUERY (index rebuilt at mount).
 *    --repeat N repeats the cycle; result = count of the first QUERY. --mount-opts adds
 *    options after lower= (the v1 QUERY needs domain_depth=0 for unprivileged use).
 * Lines are split among --procs processes by index modulo N. One record file
 * per process: <PREFIX>.<proc>.rec, 32-byte records (struct bench_rec).
 * Errors are recorded with result = -errno and never counted as latencies.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <grp.h>
#include "common.h"
#include "../include/moobfs_attr.h"
#include "../tools/query-common.h"

static const char *manifest, *target, *out, *queries, *root, *lower;
static const char *mount_opts;
static int use_query2;	/* W3: MOOB_IOC_QUERY2 on --root, a domain directory */
static const char *q2_order;	/* W3 v2: NAME:TYPE:asc|desc */
static unsigned q2_limit = MOOB_QUERY_MAX_LIMIT;	/* extra layer mount options for W6/W7, e.g. domain_depth=0 */
static int workload, nprocs = 1, repeat = 1, run_uid = -1;
static FILE *recf;
static unsigned proc_id;

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void record(int op, uint64_t start, int32_t result)
{
	struct bench_rec r = { .workload = (uint32_t)workload, .op = (uint32_t)op, .start_ns = start,
			       .dur_ns = now_ns() - start, .proc = proc_id, .result = result };

	fwrite(&r, sizeof r, 1, recf);
}

/* Encodes one manifest attribute as the typed xattr value; returns length or -1. */
static int encode(const struct bench_attr *a, ma_u8 *buf, size_t cap)
{
	size_t n;

	switch (a->type) {
	case 's':
		n = strlen(a->value);
		if (n + 1 > cap) return -1;
		buf[0] = MOOB_T_STRING; memcpy(buf + 1, a->value, n); return (int)(n + 1);
	case 'i': buf[0] = MOOB_T_INT64; moob_attr_put64(buf + 1, (ma_u64)strtoll(a->value, NULL, 10)); return 9;
	case 't': buf[0] = MOOB_T_TIME; moob_attr_put64(buf + 1, (ma_u64)strtoll(a->value, NULL, 10)); return 9;
	case 'd': {
		double d = strtod(a->value, NULL); ma_u64 bits;
		memcpy(&bits, &d, 8); buf[0] = MOOB_T_DOUBLE; moob_attr_put64(buf + 1, bits); return 9;
	}
	case 'b': buf[0] = MOOB_T_BOOL; buf[1] = (ma_u8)(a->value[0] == '1'); return 2;
	case 'r':
		if (cap < 257) return -1;
		buf[0] = MOOB_T_RAW; bench_fill(buf + 1, 256, strtoull(a->value, NULL, 10)); return 257;
	}
	return -1;
}

static int set_attr(const char *path, const struct bench_attr *a)
{
	char name[MOOB_ATTR_PREFIX_LEN + 65];
	ma_u8 buf[MOOB_ATTR_VALUE_MAX + 1];
	int n = encode(a, buf, sizeof buf);

	if (n < 0) { errno = EINVAL; return -1; }
	snprintf(name, sizeof name, "%s%s", MOOB_ATTR_PREFIX, a->name);
	return setxattr(path, name, buf, (size_t)n, 0);
}

static int mkdirs(const char *full)
{
	char tmp[4096], *p;

	snprintf(tmp, sizeof tmp, "%s", full);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/') continue;
		*p = 0;
		if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
		*p = '/';
	}
	return 0;
}

static int w1_create(const struct bench_line *l, uint64_t content_seed)
{
	char full[4096], tmp[4112], dir[4096], *slash;
	uint8_t *buf;
	int fd, dfd, i, err = 0;
	uint64_t t0;

	snprintf(full, sizeof full, "%s/%s", target, l->path);
	snprintf(tmp, sizeof tmp, "%s.tmp", full);
	snprintf(dir, sizeof dir, "%s", full);
	slash = strrchr(dir, '/'); *slash = 0;
	if (mkdirs(full)) return -1;
	buf = malloc(l->size);
	if (!buf) return -1;
	bench_fill(buf, l->size, content_seed);
	t0 = now_ns();
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) { err = -errno; goto out; }
	if (write(fd, buf, l->size) != (ssize_t)l->size) { err = -errno; close(fd); goto out; }
	for (i = 0; i < l->nattrs; i++)
		if (set_attr(tmp, &l->attrs[i])) { err = -errno; break; }
	if (!err && fsync(fd)) err = -errno;
	close(fd);
	if (!err && rename(tmp, full)) err = -errno;
	if (!err) {
		dfd = open(dir, O_RDONLY | O_DIRECTORY);
		if (dfd < 0 || fsync(dfd)) err = -errno;
		if (dfd >= 0) close(dfd);
	}
out:
	record(BENCH_OP_CREATE, t0, err ? err : (int32_t)l->size);
	free(buf);
	return err ? -1 : 0;
}

static int w2_read(const struct bench_line *l)
{
	char full[4096];
	static uint8_t buf[65536];
	ssize_t n, total = 0;
	int fd, err = 0;
	uint64_t t0 = now_ns();

	snprintf(full, sizeof full, "%s/%s", target, l->path);
	fd = open(full, O_RDONLY);
	if (fd < 0) err = -errno;
	else {
		while ((n = read(fd, buf, sizeof buf)) > 0) total += n;
		if (n < 0) err = -errno;
		close(fd);
	}
	record(BENCH_OP_READ, t0, err ? err : (int32_t)total);
	return err ? -1 : 0;
}

static int w4_attrs(const struct bench_line *l)
{
	char full[4096];
	struct bench_attr a = { .name = "score", .type = 'i' };
	struct bench_attr sc = { .name = "scratch", .type = 'b', .value = "1" };
	int i, err = 0;
	uint64_t t0;

	snprintf(full, sizeof full, "%s/%s", target, l->path);
	for (i = 0; i < l->nattrs; i++)
		if (!strcmp(l->attrs[i].name, "score")) {
			snprintf(a.value, sizeof a.value, "%lld", strtoll(l->attrs[i].value, NULL, 10) + 1);
			break;
		}
	if (i == l->nattrs) strcpy(a.value, "1");
	t0 = now_ns();
	if (set_attr(full, &a)) err = -errno;
	record(BENCH_OP_ATTR_SET, t0, err ? err : 0);
	t0 = now_ns();
	err = 0;
	if (set_attr(full, &sc) || removexattr(full, MOOB_ATTR_PREFIX "scratch")) err = -errno;
	record(BENCH_OP_ATTR_RM, t0, err ? err : 0);
	return err ? -1 : 0;
}

/* W8 helpers: the query lines are loaded once per process; one is issued per pick. */
static struct moob_query *w8q;
static int w8nq;

static int w8_load_queries(void)
{
	FILE *qf;
	char line[8192];
	int cap = 0;

	if (!queries) return 0;
	qf = fopen(queries, "r");
	if (!qf) { perror(queries); return -1; }
	while (fgets(line, sizeof line, qf)) {
		char *argv[32], *save = NULL, *tok;
		int argc = 0, n;

		if (line[0] == '#' || line[0] == '\n') continue;
		if (w8nq == cap) {
			cap = cap ? cap * 2 : 16;
			w8q = realloc(w8q, (size_t)cap * sizeof *w8q);
			if (!w8q) { fclose(qf); return -1; }
		}
		for (tok = strtok_r(line, " \t\n", &save); tok && argc < 32; tok = strtok_r(NULL, " \t\n", &save))
			argv[argc++] = tok;
		memset(&w8q[w8nq], 0, sizeof w8q[0]);
		n = parse_terms(argc, argv, 0, w8q[w8nq].terms);
		if (n >= 1) { w8q[w8nq].nterms = (__u32)n; w8nq++; }
	}
	fclose(qf);
	return 0;
}

static int w8_one_query(int fd, struct moob_query_result *res, uint64_t *rng)
{
	int pick = (int)(bench_next(rng) % (uint64_t)w8nq);
	struct moob_query q = w8q[pick];
	uint64_t t0 = now_ns();
	int32_t result;

	q.version = MOOB_UAPI_VERSION; q.limit = MOOB_QUERY_MAX_LIMIT; q.results = (__u64)(uintptr_t)res;
	if (ioctl(fd, MOOB_IOC_QUERY, &q)) result = -errno;
	else result = q.truncated ? -EOVERFLOW : (int32_t)q.count;
	record((int)BENCH_QUERY_OP(pick + 1), t0, result);
	return result < 0 ? -1 : 0;
}

static int w5_mutate(const struct bench_line *l, uint64_t content_seed)
{
	char full[4096], away[4112];
	uint64_t t0;
	int err = 0;

	snprintf(full, sizeof full, "%s/%s", target, l->path);
	snprintf(away, sizeof away, "%s.away", full);
	t0 = now_ns();
	if (rename(full, away) || rename(away, full)) err = -errno;
	record(BENCH_OP_RENAME, t0, err ? err : 0);
	t0 = now_ns();
	if (unlink(full)) { record(BENCH_OP_RECREATE, t0, -errno); return -1; }
	{
		/* recreate with the same bytes and attributes, through the W1 path but recorded as RECREATE */
		uint8_t *buf = malloc(l->size);
		int fd, i;

		if (!buf) { record(BENCH_OP_RECREATE, t0, -ENOMEM); return -1; }
		bench_fill(buf, l->size, content_seed);
		fd = open(full, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || write(fd, buf, l->size) != (ssize_t)l->size) err = -errno;
		for (i = 0; !err && i < l->nattrs; i++)
			if (set_attr(full, &l->attrs[i])) err = -errno;
		if (fd >= 0) close(fd);
		free(buf);
	}
	record(BENCH_OP_RECREATE, t0, err ? err : 0);
	return err ? -1 : 0;
}

static int w5_reader(pid_t *kids, int nkids)
{
	int fd, alive = nkids, rc = 0;
	struct moob_query_result *res;

	if (w8_load_queries() || !w8nq) { fprintf(stderr, "load: W5 needs query lines\n"); return 1; }
	fd = open(root, O_RDONLY | O_DIRECTORY);
	if (fd < 0) { perror(root); return 1; }
	res = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *res);
	if (!res) return 1;
	while (alive > 0) {
		int i, status;
		uint64_t rng = 1;

		for (i = 0; i < w8nq; i++) {
			struct moob_query q = w8q[i];
			uint64_t t0 = now_ns();
			int32_t result;

			q.version = MOOB_UAPI_VERSION; q.limit = MOOB_QUERY_MAX_LIMIT; q.results = (__u64)(uintptr_t)res;
			if (ioctl(fd, MOOB_IOC_QUERY, &q)) { result = -errno; rc = 1; }
			else result = q.truncated ? -EOVERFLOW : (int32_t)q.count;
			record((int)BENCH_QUERY_OP(i + 1), t0, result);
		}
		(void)rng;
		for (i = 0; i < nkids; i++)
			if (kids[i] > 0 && waitpid(kids[i], &status, WNOHANG) == kids[i]) {
				if (!WIFEXITED(status) || WEXITSTATUS(status)) rc = 1;
				kids[i] = 0; alive--;
			}
	}
	free(res); close(fd);
	return rc;
}

/* --order NAME:TYPE:asc|desc for W3 with --query2 (an ordered "N most recent" shape). */
static int q2_order_set(struct moob_query2 *q2)
{
	char buf[MOOB_QUERY_NAME_MAX + 32], *t, *d;
	int type;

	size_t olen = strlen(q2_order);

	if (olen >= sizeof buf) return -1;
	memcpy(buf, q2_order, olen + 1);
	t = strchr(buf, ':'); if (!t) return -1; *t++ = 0;
	d = strchr(t, ':'); if (!d) return -1; *d++ = 0;
	type = parse_type(t);
	if (type < 0 || strlen(buf) >= sizeof q2->order_by) return -1;
	memcpy(q2->order_by, buf, strlen(buf) + 1);
	q2->order_type = (__u8)type;
	q2->order = !strcmp(d, "desc") ? MOOB_ORDER_DESC : MOOB_ORDER_ASC;
	return 0;
}

static int w3_queries(void)
{
	struct moob_query_result *res;
	int fd, r, i, k = 0;

	/* the query file is parsed once (W8 helper); every shape runs --repeat times, in file order,
	 * split among processes by shape index so each process always measures the same shapes */
	if (!w8nq && (w8_load_queries() || !w8nq)) { fprintf(stderr, "load: W3 needs query lines\n"); return -1; }
	fd = open(root ? root : target, O_RDONLY | O_DIRECTORY);
	if (fd < 0) { perror("open root"); return -1; }
	res = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *res);
	if (!res) { close(fd); return -1; }
	for (r = 0; r < repeat; r++)
		for (i = 0; i < w8nq; i++) {
			struct moob_query q = w8q[i];
			uint64_t t0;
			int32_t result;

			if ((k++ % nprocs) != (int)proc_id) continue;
			if (use_query2) {
				struct moob_query2 q2;

				memset(&q2, 0, sizeof q2);
				q2.version = MOOB_QUERY2_VERSION; q2.nterms = q.nterms;
				q2.limit = q2_limit; q2.results = (__u64)(uintptr_t)res;
				memcpy(q2.terms, q.terms, sizeof q2.terms);
				if (q2_order && q2_order_set(&q2)) { result = -EINVAL; t0 = now_ns(); record((int)BENCH_QUERY_OP(i + 1), t0, result); continue; }
				t0 = now_ns();
				if (ioctl(fd, MOOB_IOC_QUERY2, &q2)) result = -errno;
				else result = q2.truncated ? -EOVERFLOW : (int32_t)q2.count;
				record((int)BENCH_QUERY_OP(i + 1), t0, result);
				continue;
			}
			q.version = MOOB_UAPI_VERSION; q.limit = MOOB_QUERY_MAX_LIMIT;
			q.results = (__u64)(uintptr_t)res;
			t0 = now_ns();
			if (ioctl(fd, MOOB_IOC_QUERY, &q)) result = -errno;
			else result = q.truncated ? -EOVERFLOW : (int32_t)q.count;
			record((int)BENCH_QUERY_OP(i + 1), t0, result);
		}
	free(res); close(fd);
	return 0;
}

/* Reads the first query line of --queries into q; returns 0 or -1. */
static int first_query(struct moob_query *q)
{
	FILE *qf = fopen(queries, "r");
	char line[8192];
	int ok = -1;

	if (!qf) { perror(queries); return -1; }
	while (fgets(line, sizeof line, qf)) {
		char *argv[32], *save = NULL, *tok;
		int argc = 0, n;

		if (line[0] == '#' || line[0] == '\n') continue;
		for (tok = strtok_r(line, " \t\n", &save); tok && argc < 32; tok = strtok_r(NULL, " \t\n", &save))
			argv[argc++] = tok;
		memset(q, 0, sizeof *q);
		n = parse_terms(argc, argv, 0, q->terms);
		if (n >= 1) { q->nterms = (__u32)n; ok = 0; }
		break;
	}
	fclose(qf);
	return ok;
}

static int w6_mount(void)
{
	struct moob_query q;
	struct moob_query_result *res;
	char opts[4200];
	int r, rc = 0;

	if (first_query(&q)) return 1;
	res = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *res);
	if (!res) return 1;
	if (snprintf(opts, sizeof opts, "lower=%s%s%s", lower, mount_opts ? "," : "",
		     mount_opts ? mount_opts : "") >= (int)sizeof opts) {
		free(res);
		return 1;
	}
	for (r = 0; r < repeat; r++) {
		uint64_t t0;
		int fd, err = 0;
		int32_t result;

		umount(target);	/* ignore ENOENT/EINVAL when nothing is mounted */
		if (workload == 6) {
			FILE *dc;
			int dcfd;

			sync();
			dcfd = open("/proc/sys/vm/drop_caches", O_WRONLY | O_CLOEXEC);
			dc = dcfd < 0 ? NULL : fdopen(dcfd, "w");
			if (dcfd >= 0 && !dc)
				close(dcfd);
			if (!dc || fputs("3\n", dc) == EOF) { perror("drop_caches"); rc = 1; }
			if (dc) fclose(dc);
		}
		t0 = now_ns();
		if (mount("none", target, "moobfs_layer", 0, opts)) { err = -errno; record(BENCH_OP_MOUNT, t0, err); rc = 1; continue; }
		fd = open(target, O_RDONLY | O_DIRECTORY);
		if (fd < 0) { record(BENCH_OP_MOUNT, t0, -errno); rc = 1; continue; }
		q.version = MOOB_UAPI_VERSION; q.limit = MOOB_QUERY_MAX_LIMIT; q.results = (__u64)(uintptr_t)res;
		q.count = 0; q.truncated = 0;
		/* W6/W7 time the mount until the index answers; an answer truncated at the UAPI
		 * limit still proves the index is ready, so it counts as a success here */
		if (ioctl(fd, MOOB_IOC_QUERY, &q)) result = -errno;
		else result = (int32_t)q.count;
		record(BENCH_OP_MOUNT, t0, result);
		close(fd);
	}
	free(res);
	return rc;
}

static int run_proc(void)
{
	FILE *mf;
	char recname[4096], *line = NULL;
	size_t cap = 0;
	uint64_t *seeds = NULL;
	size_t nseeds = 0, capseeds = 0, idx = 0;
	int failures = 0, w8fd = -1;
	struct moob_query_result *w8res = NULL;
	uint64_t rng = 0x5eed0000ULL + proc_id;

	snprintf(recname, sizeof recname, "%s.%u.rec", out, proc_id);
	if (!recf && !(recf = bench_create_out(recname))) { perror(recname); return 1; }
	if (workload == 3 || workload == 6 || workload == 7) {
		int r = workload == 3 ? w3_queries() : w6_mount();
		fclose(recf);
		return r ? 1 : 0;
	}
	if (workload == 8) {
		if (w8_load_queries()) return 1;
		if (w8nq && root) {
			w8fd = open(root, O_RDONLY | O_DIRECTORY);
			if (w8fd < 0) { perror(root); return 1; }
			w8res = calloc(MOOB_QUERY_MAX_LIMIT, sizeof *w8res);
			if (!w8res) return 1;
		}
	}
	mf = fopen(manifest, "r");
	if (!mf) { perror(manifest); return 1; }
	while (getline(&line, &cap, mf) > 0) {
		struct bench_line l;
		uint64_t cseed;

		if (line[0] == '#') continue;
		if (bench_parse_line(line, &l)) { fprintf(stderr, "load: bad manifest line %zu\n", idx + 1); return 1; }
		/* every process tracks every content seed so duplicates resolve regardless of the owner */
		if (nseeds == capseeds) {
			capseeds = capseeds ? capseeds * 2 : 1024;
			seeds = realloc(seeds, capseeds * sizeof *seeds);
			if (!seeds) return 1;
		}
		cseed = l.dup_of >= 0 && (size_t)l.dup_of < nseeds ? seeds[l.dup_of] : l.seed;
		seeds[nseeds++] = cseed;
		if (workload == 5) { if (nprocs < 2 || ((idx++ % (size_t)(nprocs - 1)) + 1) != proc_id) continue; }
		else if ((idx++ % (size_t)nprocs) != proc_id) continue;
		switch (workload) {
		case 1: if (w1_create(&l, cseed)) failures++; break;
		case 2: if (w2_read(&l)) failures++; break;
		case 4: if (w4_attrs(&l)) failures++; break;
		case 5: if (proc_id > 0 && w5_mutate(&l, cseed)) failures++; break;
		case 8: {
			uint64_t pick = bench_next(&rng) % 100;

			if (pick < 70) { if (w2_read(&l)) failures++; }
			else if (pick < 90 || w8fd < 0) { if (w4_attrs(&l)) failures++; }
			else if (w8_one_query(w8fd, w8res, &rng)) failures++;
			break;
		}
		}
	}
	free(line); free(seeds); fclose(mf); fclose(recf);
	if (w8fd >= 0) close(w8fd);
	free(w8res); free(w8q);
	if (failures)
		fprintf(stderr, "load: proc %u: %d failed operations (recorded as errors)\n", proc_id, failures);
	return 0;
}

int main(int argc, char **argv)
{
	int a, i, status, rc = 0;

	for (a = 1; a + 1 < argc; a += 2) {
		if (!strcmp(argv[a], "--manifest")) manifest = argv[a + 1];
		else if (!strcmp(argv[a], "--target")) target = argv[a + 1];
		else if (!strcmp(argv[a], "--out")) out = argv[a + 1];
		else if (!strcmp(argv[a], "--queries")) queries = argv[a + 1];
		else if (!strcmp(argv[a], "--root")) root = argv[a + 1];
		else if (!strcmp(argv[a], "--procs")) nprocs = atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--lower")) lower = argv[a + 1];
		else if (!strcmp(argv[a], "--mount-opts")) mount_opts = argv[a + 1];
		else if (!strcmp(argv[a], "--query2")) use_query2 = atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--order")) q2_order = argv[a + 1];
		else if (!strcmp(argv[a], "--limit")) q2_limit = (unsigned)atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--repeat")) repeat = atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--uid")) run_uid = atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--workload") && argv[a + 1][0] == 'W') workload = atoi(argv[a + 1] + 1);
		else { fprintf(stderr, "load: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || !target || !out || nprocs < 1 || nprocs > 1024 ||
	    !(workload == 1 || workload == 2 || workload == 3 || workload == 4 || workload == 5 || workload == 6 || workload == 7 || workload == 8) ||
	    (workload == 3 ? !queries : (workload == 6 || workload == 7) ? (!queries || !lower || repeat < 1) :
	     workload == 5 ? (!manifest || !queries || !root || nprocs < 2) : !manifest)) {
		fprintf(stderr, "usage: load --manifest FILE --target DIR --workload W1|W2|W3|W4|W5|W8 --out PREFIX [--procs N] [--queries FILE] [--root DIR] [--uid N] [--query2 1 [--order NAME:TYPE:asc|desc] [--limit N]]\n"
				"       load --workload W6|W7 --target MOUNT --lower DIR --queries FILE --out PREFIX [--repeat N] [--mount-opts OPTS]   (root)\n");
		return 64;
	}
	if (run_uid >= 0) {
		/* the query file and the record file are opened as the invoking user (root), then
		 * privileges are dropped, so only the QUERY itself runs with the regular credentials */
		char recname[4096];

		if (workload != 3 || nprocs != 1) { fprintf(stderr, "load: --uid is for W3 with one process\n"); return 64; }
		if (w8_load_queries() || !w8nq) { fprintf(stderr, "load: W3 needs query lines\n"); return 1; }
		snprintf(recname, sizeof recname, "%s.0.rec", out);
		if (!(recf = bench_create_out(recname))) { perror(recname); return 1; }
		if (setgroups(0, NULL) || setgid((gid_t)run_uid) || setuid((uid_t)run_uid)) { perror("setuid"); return 1; }
	}
	if (nprocs == 1 || workload == 6 || workload == 7) { proc_id = 0; return run_proc(); }
	if (workload == 5) {
		pid_t kids[1024];
		char recname[4096];

		for (i = 1; i < nprocs; i++) {
			kids[i - 1] = fork();
			if (kids[i - 1] < 0) { perror("fork"); return 1; }
			if (!kids[i - 1]) { proc_id = (unsigned)i; _exit(run_proc()); }
		}
		proc_id = 0;
		snprintf(recname, sizeof recname, "%s.0.rec", out);
		recf = bench_create_out(recname);
		if (!recf) { perror(recname); return 1; }
		rc = w5_reader(kids, nprocs - 1);
		fclose(recf);
		return rc;
	}
	for (i = 0; i < nprocs; i++) {
		pid_t pid = fork();

		if (pid < 0) { perror("fork"); return 1; }
		if (!pid) { proc_id = (unsigned)i; _exit(run_proc()); }
	}
	for (i = 0; i < nprocs; i++)
		if (wait(&status) > 0 && (!WIFEXITED(status) || WEXITSTATUS(status))) rc = 1;
	return rc;
}
