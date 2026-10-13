/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bench/client/aimem-latency: stage 2 latency of docs/client-test-plan.md. Runs the six query
 * shapes of aimem-compare.py in one process on both sides: ai-memory's SQL on its own SQLite
 * (through libsqlite3, statements prepared once, as ai-memory's `prepare_cached`) and QUERY2 on
 * the project directory (a domain, domain_depth=2), and records one latency sample per call.
 * Answers are checked by aimem-compare.py; here only the row counts of the two sides are compared.
 *
 *   aimem-latency --db DATA_DIR/db/memory.sqlite --wiki WIKI_ON_THE_LAYER --out FILE.csv
 *                 [--repeat 200] [--cold 0] [--limit 20] [--page 256] [--max-projects 0]
 *                 [--now-us US] [--stale-days 7]
 *
 * Warm: per project, one connection and one descriptor of the domain directory stay open, every
 * shape runs once unmeasured, then --repeat times on each side, the sides alternating.
 * Cold (--cold N, needs root): before every sample the connection and the descriptor are closed
 * and the page, dentry and inode caches are dropped; the sample then covers what a client that
 * just started pays: open the database, prepare and run on one side, open the directory and run
 * on the other; open_ns is the part before the first row is asked for (open and prepare, or
 * open of the directory). The index of the layer lives in kernel memory and is not dropped.
 *
 * Timed on the SQL side: bind, step through every row reading the path, reset. Timed on the
 * QUERY2 side: the ioctl calls. `incremental` reads every page updated after the project's
 * median: one statement in SQL, the cursor with --page results per call in QUERY2. `settled` is
 * one statement in SQL and one QUERY2_BATCH of two requests. Prints p50/p95/p99 per mode, shape
 * and side (open_p50 and run_p50 split the cold samples); --out gets one row per sample
 * (mode,side,shape,project,rep,ns,open_ns,rows).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sqlite3.h>
#include "../common.h"
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

enum { RECENT, PINNED, RULES, SETTLED, STALE, INCREMENTAL, NSHAPES };
enum { SQL, Q2, NSIDES };
enum { WARM, COLD, NMODES };

static const char *const shape_name[NSHAPES] = { "recent", "pinned", "rules", "settled", "stale", "incremental" };
static const char *const side_name[NSIDES] = { "sql", "query2" };
static const char *const mode_name[NMODES] = { "warm", "cold" };

#define SCOPE "workspace_id = :ws AND project_id = :proj AND is_latest = 1"
#define NOT_EXPIRED " AND (expires_at IS NULL OR expires_at > :now)"
/* page_kind_expr of ai-memory store/src/reader.rs, as in aimem-compare.py */
#define KIND "COALESCE(json_extract(frontmatter_json, '$.kind'), CASE " \
	"WHEN path LIKE '\\_rules/%' ESCAPE '\\' THEN 'rule' WHEN path LIKE '\\_slots/%' ESCAPE '\\' THEN 'slot' " \
	"WHEN path LIKE 'sessions/%' THEN 'session' WHEN path LIKE 'decisions/%' THEN 'decision' " \
	"WHEN path LIKE 'gotchas/%' THEN 'gotcha' WHEN path LIKE 'concepts/%' THEN 'concept' " \
	"WHEN path LIKE 'procedures/%' THEN 'procedure' WHEN path LIKE 'notes/%' THEN 'note' " \
	"WHEN path LIKE 'profile/%' THEN 'preference' ELSE 'fact' END)"

static const char *const shape_sql[NSHAPES] = {
	[RECENT] = "SELECT path FROM pages WHERE " SCOPE NOT_EXPIRED " ORDER BY updated_at DESC LIMIT :lim",
	[PINNED] = "SELECT path FROM pages WHERE " SCOPE " AND pinned = 1" NOT_EXPIRED
		   " ORDER BY updated_at DESC LIMIT :lim",
	[RULES] = "SELECT path FROM pages WHERE " SCOPE " AND path LIKE '\\_rules/%' ESCAPE '\\'" NOT_EXPIRED
		  " ORDER BY updated_at DESC LIMIT :lim",
	[SETTLED] = "SELECT path FROM pages WHERE " SCOPE " AND " KIND " IN ('rule', 'decision')" NOT_EXPIRED,
	[STALE] = "SELECT path FROM pages WHERE " SCOPE " AND tier = 'episodic' AND updated_at < :cut"
		  " ORDER BY updated_at ASC LIMIT :lim",
	[INCREMENTAL] = "SELECT path FROM pages WHERE " SCOPE " AND updated_at > :mid" NOT_EXPIRED
			" ORDER BY updated_at ASC",
};

struct project {
	uint8_t ws[16], proj[16];
	char name[80];		/* <workspace-uuid>/<project-uuid> */
	char dir[4096];
	int64_t mid, cut;	/* microseconds, as updated_at in the store */
	struct moob_query2 q[NSHAPES + 1];	/* SETTLED uses q[SETTLED] and q[NSHAPES] */
};

struct sample {
	uint8_t mode, side, shape;
	uint32_t project, rep, rows;
	uint64_t ns, open_ns;
};

static const char *db_path;
static int64_t now_us, limit = 20, page = 256;
static struct moob_query_result *res[2];
static struct sample *samples;
static size_t nsamples, cap_samples;
static unsigned long mismatches, unknown_paths;

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void die(const char *what, const char *detail)
{
	fprintf(stderr, "aimem-latency: %s: %s\n", what, detail);
	exit(1);
}

static void add_sample(int mode, int side, int shape, size_t project, int rep, uint64_t ns, uint64_t open_ns,
		       uint32_t rows)
{
	struct sample *s;

	if (nsamples == cap_samples) {
		cap_samples = cap_samples ? cap_samples * 2 : 4096;
		samples = realloc(samples, cap_samples * sizeof *samples);
		if (!samples) die("samples", "out of memory");
	}
	s = &samples[nsamples++];
	s->mode = (uint8_t)mode; s->side = (uint8_t)side; s->shape = (uint8_t)shape;
	s->project = (uint32_t)project; s->rep = (uint32_t)rep; s->rows = rows; s->ns = ns;
	s->open_ns = open_ns;
}

static void uuid_text(const uint8_t *b, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	for (i = 0; i < 16; i++) {
		if (i == 4 || i == 6 || i == 8 || i == 10)
			*out++ = '-';
		*out++ = hex[b[i] >> 4];
		*out++ = hex[b[i] & 15];
	}
	*out = 0;
}

static struct moob_query_term *add_term(struct moob_query2 *q, const char *name, int op, int type, int missing_ok)
{
	struct moob_query_term *t = &q->terms[q->nterms++];

	memcpy(t->name, name, strlen(name));
	t->op = (__u8)op;
	t->type = (__u8)type;
	t->flags = missing_ok ? MOOB_TERM_MISSING_OK : 0;
	return t;
}

static void term_time(struct moob_query2 *q, const char *name, int op, int missing_ok, int64_t us)
{
	struct moob_query_term *t = add_term(q, name, op, MOOB_T_TIME, missing_ok);

	moob_attr_put64(t->value, (ma_u64)(us * 1000));
	t->len = 8;
}

static void term_str(struct moob_query2 *q, const char *name, const char *v)
{
	struct moob_query_term *t = add_term(q, name, MOOB_OP_EQ, MOOB_T_STRING, 0);

	memcpy(t->value, v, strlen(v));
	t->len = (__u16)strlen(v);
}

static void q2_init(struct moob_query2 *q, int order, int64_t lim, int slot)
{
	memset(q, 0, sizeof *q);
	q->version = MOOB_QUERY2_VERSION;
	q->limit = (__u32)lim;
	q->results = (__u64)(uintptr_t)res[slot];
	if (order) {
		memcpy(q->order_by, "updated_at", sizeof "updated_at");
		q->order_type = MOOB_T_TIME;
		q->order = (__u8)order;
	}
}

/* The QUERY2 requests of aimem-compare.py; "not expired" is expires_at > now or no expires_at. */
static void build_queries(struct project *p)
{
	struct moob_query2 *q = p->q;
	struct moob_query_term *t;

	q2_init(&q[RECENT], MOOB_ORDER_DESC, limit, 0);
	term_time(&q[RECENT], "expires_at", MOOB_OP_GT, 1, now_us);
	q2_init(&q[PINNED], MOOB_ORDER_DESC, limit, 0);
	t = add_term(&q[PINNED], "pinned", MOOB_OP_EQ, MOOB_T_BOOL, 0);
	t->value[0] = 1; t->len = 1;
	term_time(&q[PINNED], "expires_at", MOOB_OP_GT, 1, now_us);
	q2_init(&q[RULES], MOOB_ORDER_DESC, limit, 0);
	term_str(&q[RULES], "family", "_rules");
	term_time(&q[RULES], "expires_at", MOOB_OP_GT, 1, now_us);
	q2_init(&q[SETTLED], 0, MOOB_QUERY_MAX_LIMIT, 0);
	term_str(&q[SETTLED], "kind", "rule");
	term_time(&q[SETTLED], "expires_at", MOOB_OP_GT, 1, now_us);
	q2_init(&q[NSHAPES], 0, MOOB_QUERY_MAX_LIMIT, 1);
	term_str(&q[NSHAPES], "kind", "decision");
	term_time(&q[NSHAPES], "expires_at", MOOB_OP_GT, 1, now_us);
	q2_init(&q[STALE], MOOB_ORDER_ASC, limit, 0);
	term_str(&q[STALE], "tier", "episodic");
	term_time(&q[STALE], "updated_at", MOOB_OP_LT, 0, p->cut);
	q2_init(&q[INCREMENTAL], MOOB_ORDER_ASC, page, 0);
	term_time(&q[INCREMENTAL], "updated_at", MOOB_OP_GT, 0, p->mid);
	term_time(&q[INCREMENTAL], "expires_at", MOOB_OP_GT, 1, now_us);
}

static sqlite3 *db_open(void)
{
	sqlite3 *h;

	if (sqlite3_open_v2(db_path, &h, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
		die(db_path, sqlite3_errmsg(h));
	return h;
}

static sqlite3_stmt *prepare(sqlite3 *h, const char *sql)
{
	sqlite3_stmt *st;

	if (sqlite3_prepare_v2(h, sql, -1, &st, NULL) != SQLITE_OK)
		die("prepare", sqlite3_errmsg(h));
	return st;
}

static void bind64(sqlite3_stmt *st, const char *name, int64_t v)
{
	int i = sqlite3_bind_parameter_index(st, name);

	if (i)
		sqlite3_bind_int64(st, i, v);
}

/* Binds, reads every row and resets; returns the row count. */
static uint32_t run_sql(sqlite3 *h, sqlite3_stmt *st, const struct project *p)
{
	uint32_t rows = 0;
	size_t bytes = 0;
	int step;

	sqlite3_bind_blob(st, sqlite3_bind_parameter_index(st, ":ws"), p->ws, 16, SQLITE_STATIC);
	sqlite3_bind_blob(st, sqlite3_bind_parameter_index(st, ":proj"), p->proj, 16, SQLITE_STATIC);
	bind64(st, ":now", now_us);
	bind64(st, ":lim", limit);
	bind64(st, ":cut", p->cut);
	bind64(st, ":mid", p->mid);
	while ((step = sqlite3_step(st)) == SQLITE_ROW) {
		/* the client copies the path out of the row; QUERY2 copies it into the result */
		if (sqlite3_column_text(st, 0))
			bytes += (size_t)sqlite3_column_bytes(st, 0);
		rows++;
	}
	if (step != SQLITE_DONE)
		die("step", sqlite3_errmsg(h));
	sqlite3_reset(st);
	(void)bytes;
	return rows;
}

static void count_unknown(const struct moob_query2 *q)
{
	const struct moob_query_result *r = (const struct moob_query_result *)(uintptr_t)q->results;
	__u32 i;

	for (i = 0; i < q->count; i++)
		if (r[i].flags & MOOB_RES_PATH_UNKNOWN)
			unknown_paths++;
}

/* Issues the shape's QUERY2 request(s) on fd; *ns gets the time inside ioctl(). */
static uint32_t run_q2(int fd, struct project *p, int shape, uint64_t *ns)
{
	struct moob_query2 *q = &p->q[shape];
	uint64_t t0 = now_ns();
	uint32_t rows = 0;

	if (shape == SETTLED) {
		struct moob_query2 pair[2];
		struct moob_query2_batch b;

		pair[0] = p->q[SETTLED]; pair[1] = p->q[NSHAPES];
		memset(&b, 0, sizeof b);
		b.version = MOOB_QUERY2_VERSION; b.nqueries = 2; b.queries = (__u64)(uintptr_t)pair;
		t0 = now_ns();
		if (ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b))
			die("MOOB_IOC_QUERY2_BATCH", strerror(errno));
		*ns = now_ns() - t0;
		if (pair[0].truncated || pair[1].truncated)
			die(p->name, "settled does not fit one QUERY2_BATCH reply");
		count_unknown(&pair[0]); count_unknown(&pair[1]);
		return pair[0].count + pair[1].count;
	}
	memset(q->cursor, 0, sizeof q->cursor);
	*ns = 0;
	for (;;) {
		t0 = now_ns();
		if (ioctl(fd, MOOB_IOC_QUERY2, q))
			die("MOOB_IOC_QUERY2", strerror(errno));
		*ns += now_ns() - t0;
		rows += q->count;
		count_unknown(q);
		if (!q->truncated || shape != INCREMENTAL)
			break;
	}
	return rows;
}

static void drop_caches(void)
{
	int fd;

	sync();
	fd = open("/proc/sys/vm/drop_caches", O_WRONLY | O_CLOEXEC);
	if (fd < 0 || write(fd, "3\n", 2) != 2)
		die("/proc/sys/vm/drop_caches", strerror(errno));
	close(fd);
}

static int open_dir(const struct project *p)
{
	int fd = open(p->dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd < 0)
		die(p->dir, strerror(errno));
	return fd;
}

static void check_rows(const struct project *p, int shape, uint32_t sql, uint32_t q2)
{
	if (sql == q2)
		return;
	if (mismatches++ < 10)
		fprintf(stderr, "aimem-latency: %s %s: sql=%u query2=%u rows\n", p->name, shape_name[shape], sql, q2);
}

static void warm(struct project *p, size_t pi, int repeat)
{
	sqlite3 *h = db_open();
	sqlite3_stmt *st[NSHAPES];
	int fd = open_dir(p), shape, rep, k;
	uint64_t ns;

	for (shape = 0; shape < NSHAPES; shape++) {
		st[shape] = prepare(h, shape_sql[shape]);
		run_sql(h, st[shape], p);
		run_q2(fd, p, shape, &ns);
	}
	for (rep = 0; rep < repeat; rep++) {
		for (shape = 0; shape < NSHAPES; shape++) {
			uint32_t rows[NSIDES] = { 0, 0 };

			for (k = 0; k < NSIDES; k++) {
				int side = (rep + k) % NSIDES;

				if (side == SQL) {
					uint64_t t0 = now_ns();

					rows[SQL] = run_sql(h, st[shape], p);
					ns = now_ns() - t0;
				} else {
					rows[Q2] = run_q2(fd, p, shape, &ns);
				}
				add_sample(WARM, side, shape, pi, rep, ns, 0, rows[side]);
			}
			check_rows(p, shape, rows[SQL], rows[Q2]);
		}
	}
	for (shape = 0; shape < NSHAPES; shape++)
		sqlite3_finalize(st[shape]);
	sqlite3_close(h);
	close(fd);
}

static void cold(struct project *p, size_t pi, int rep)
{
	int shape;

	for (shape = 0; shape < NSHAPES; shape++) {
		uint32_t rows[NSIDES];
		sqlite3_stmt *st;
		uint64_t t0, t1, ns;
		sqlite3 *h;
		int fd;

		drop_caches();
		t0 = now_ns();
		h = db_open();
		st = prepare(h, shape_sql[shape]);
		t1 = now_ns();
		rows[SQL] = run_sql(h, st, p);
		ns = now_ns() - t0;
		sqlite3_finalize(st);
		sqlite3_close(h);
		add_sample(COLD, SQL, shape, pi, rep, ns, t1 - t0, rows[SQL]);

		drop_caches();
		t0 = now_ns();
		fd = open_dir(p);
		t1 = now_ns();
		rows[Q2] = run_q2(fd, p, shape, &ns);
		ns = now_ns() - t0;
		close(fd);
		add_sample(COLD, Q2, shape, pi, rep, ns, t1 - t0, rows[Q2]);
		check_rows(p, shape, rows[SQL], rows[Q2]);
	}
}

/* Projects with latest pages whose directory exists under wiki, with the median and the cutoff. */
static struct project *load_projects(const char *wiki, int64_t stale_days, size_t max, size_t *n)
{
	sqlite3 *h = db_open();
	sqlite3_stmt *list = prepare(h, "SELECT workspace_id, project_id, COUNT(*), MIN(updated_at) FROM pages "
					"WHERE is_latest = 1 GROUP BY 1, 2 ORDER BY 1, 2");
	sqlite3_stmt *med = prepare(h, "SELECT updated_at FROM pages WHERE " SCOPE
				       " ORDER BY updated_at LIMIT 1 OFFSET :off");
	struct project *ps = NULL;
	size_t cap = 0;

	*n = 0;
	while (sqlite3_step(list) == SQLITE_ROW && (!max || *n < max)) {
		struct project *p;
		char ws[40], proj[40];
		int64_t cutoff = now_us - stale_days * 86400 * 1000000;

		if (sqlite3_column_bytes(list, 0) != 16 || sqlite3_column_bytes(list, 1) != 16)
			continue;
		if (*n == cap) {
			cap = cap ? cap * 2 : 16;
			ps = realloc(ps, cap * sizeof *ps);
			if (!ps) die("projects", "out of memory");
		}
		p = &ps[*n];
		memset(p, 0, sizeof *p);
		memcpy(p->ws, sqlite3_column_blob(list, 0), 16);
		memcpy(p->proj, sqlite3_column_blob(list, 1), 16);
		uuid_text(p->ws, ws); uuid_text(p->proj, proj);
		snprintf(p->name, sizeof p->name, "%s/%s", ws, proj);
		if (snprintf(p->dir, sizeof p->dir, "%s/%s", wiki, p->name) >= (int)sizeof p->dir ||
		    access(p->dir, F_OK))
			continue;
		sqlite3_bind_blob(med, sqlite3_bind_parameter_index(med, ":ws"), p->ws, 16, SQLITE_STATIC);
		sqlite3_bind_blob(med, sqlite3_bind_parameter_index(med, ":proj"), p->proj, 16, SQLITE_STATIC);
		bind64(med, ":off", sqlite3_column_int64(list, 2) / 2);
		if (sqlite3_step(med) != SQLITE_ROW)
			die("median", sqlite3_errmsg(h));
		p->mid = sqlite3_column_int64(med, 0);
		sqlite3_reset(med);
		/* as aimem-compare.py: when no page is older than the cutoff, the median splits them */
		p->cut = sqlite3_column_int64(list, 3) < cutoff ? cutoff : p->mid;
		build_queries(p);
		(*n)++;
	}
	sqlite3_finalize(list);
	sqlite3_finalize(med);
	sqlite3_close(h);
	return ps;
}

static int by_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

static void summary(void)
{
	uint64_t *v = malloc((nsamples + 1) * sizeof *v), *o = malloc((nsamples + 1) * sizeof *o),
		 *r = malloc((nsamples + 1) * sizeof *r);
	double p50[NSIDES] = { 0, 0 };
	int mode, shape, side;
	size_t i;

	if (!v || !o || !r) die("summary", "out of memory");
	puts("mode\tshape\tside\tsamples\trows_mean\tp50_us\tp95_us\tp99_us\topen_p50_us\trun_p50_us");
	for (mode = 0; mode < NMODES; mode++) {
		for (shape = 0; shape < NSHAPES; shape++) {
			size_t n = 0;

			for (side = 0; side < NSIDES; side++) {
				uint64_t rows = 0;

				n = 0;
				for (i = 0; i < nsamples; i++) {
					const struct sample *s = &samples[i];

					if (s->mode != mode || s->shape != shape || s->side != side)
						continue;
					o[n] = s->open_ns;
					r[n] = s->ns - s->open_ns;
					v[n++] = s->ns;
					rows += s->rows;
				}
				if (!n)
					break;
				qsort(v, n, sizeof *v, by_u64);
				qsort(o, n, sizeof *o, by_u64);
				qsort(r, n, sizeof *r, by_u64);
				p50[side] = (double)v[n / 2] / 1e3;
				printf("%s\t%s\t%s\t%zu\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\n", mode_name[mode], shape_name[shape],
				       side_name[side], n, (double)rows / (double)n, p50[side],
				       (double)v[n * 95 / 100] / 1e3, (double)v[n * 99 / 100] / 1e3, (double)o[n / 2] / 1e3,
				       (double)r[n / 2] / 1e3);
			}
			if (n && p50[Q2] > 0)
				printf("%s\t%s\tsql/query2\t-\t-\t%.2f\t-\t-\t-\t-\n", mode_name[mode], shape_name[shape],
				       p50[SQL] / p50[Q2]);
		}
	}
	free(v);
	free(o);
	free(r);
}

int main(int argc, char **argv)
{
	const char *wiki = NULL, *out = NULL;
	int64_t stale_days = 7;
	int a, repeat = 200, ncold = 0, rep;
	size_t max = 0, np, i;
	struct project *ps;
	FILE *f;

	for (a = 1; a + 1 < argc; a += 2) {
		const char *v = argv[a + 1];

		if (!strcmp(argv[a], "--db")) db_path = v;
		else if (!strcmp(argv[a], "--wiki")) wiki = v;
		else if (!strcmp(argv[a], "--out")) out = v;
		else if (!strcmp(argv[a], "--repeat")) repeat = atoi(v);
		else if (!strcmp(argv[a], "--cold")) ncold = atoi(v);
		else if (!strcmp(argv[a], "--limit")) limit = atoll(v);
		else if (!strcmp(argv[a], "--page")) page = atoll(v);
		else if (!strcmp(argv[a], "--max-projects")) max = (size_t)atoll(v);
		else if (!strcmp(argv[a], "--now-us")) now_us = atoll(v);
		else if (!strcmp(argv[a], "--stale-days")) stale_days = atoll(v);
		else { fprintf(stderr, "aimem-latency: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || !db_path || !wiki || !out || repeat < 0 || ncold < 0 || repeat + ncold < 1 ||
	    limit < 1 || limit > MOOB_QUERY_MAX_LIMIT || page < 1 || page > MOOB_QUERY_MAX_LIMIT) {
		fprintf(stderr, "usage: aimem-latency --db FILE --wiki DIR --out FILE.csv [--repeat N] [--cold N]\n"
				"       [--limit N] [--page N] [--max-projects N] [--now-us US] [--stale-days N]\n");
		return 64;
	}
	if (!now_us) {
		struct timespec ts;

		clock_gettime(CLOCK_REALTIME, &ts);
		now_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
	}
	res[0] = calloc(MOOB_QUERY_MAX_LIMIT, sizeof **res);
	res[1] = calloc(MOOB_QUERY_MAX_LIMIT, sizeof **res);
	if (!res[0] || !res[1]) die("results", "out of memory");
	ps = load_projects(wiki, stale_days, max, &np);
	if (!np) die(wiki, "no project of the database has a directory here");
	for (i = 0; i < np && repeat; i++)
		warm(&ps[i], i, repeat);
	for (rep = 0; rep < ncold; rep++)
		for (i = 0; i < np; i++)
			cold(&ps[i], i, rep);
	f = bench_create_out(out);
	if (!f) { perror(out); return 1; }
	fputs("mode,side,shape,project,rep,ns,open_ns,rows\n", f);
	for (i = 0; i < nsamples; i++)
		fprintf(f, "%s,%s,%s,%s,%u,%llu,%llu,%u\n", mode_name[samples[i].mode], side_name[samples[i].side],
			shape_name[samples[i].shape], ps[samples[i].project].name, samples[i].rep,
			(unsigned long long)samples[i].ns, (unsigned long long)samples[i].open_ns, samples[i].rows);
	if (fclose(f)) { perror(out); return 1; }
	summary();
	printf("LATENCY: projects=%zu samples=%zu row_mismatches=%lu unknown_paths=%lu now_us=%lld\n",
	       np, nsamples, mismatches, unknown_paths, (long long)now_us);
	return mismatches ? 1 : 0;
}
