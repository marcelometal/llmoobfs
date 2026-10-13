/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * synccost: what it costs to keep the structured fields of a memory queryable at write time,
 * three ways (journal 0091). Every memory is published durably, as the client contract asks:
 * temporary file, fsync, rename, fsync of the directory.
 *
 *   files    the file only: the floor, nothing is queryable
 *   xattr    the file with its fields as typed attributes, set before the rename. On a layer
 *            mount this is the layer's write path (validation and index update); on a plain
 *            filesystem it is what the attributes alone cost
 *   sqlite   the file, then one row in a SQLite database in the same directory tree, each in
 *            its own transaction: the side index an application keeps in step. SYNC=full makes
 *            the row as durable as the file (the default here); SYNC=normal is what most
 *            applications run, and can lose the last rows on power loss
 *
 *   synccost --dir DIR --mode files|xattr|sqlite [--n N] [--sync full|normal] [--body BYTES]
 *
 * Prints one line: mode, n, p50, p95, mean in microseconds per memory, and the total.
 */
#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sqlite3.h>

static void die(const char *what)
{
	fprintf(stderr, "synccost: %s: %s\n", what, strerror(errno));
	exit(1);
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int cmp(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

/* the typed encoding of include/moobfs_attr.h: one type byte, little-endian payload */
static void set_str(int fd, const char *name, const char *v)
{
	char full[96], buf[1100];
	size_t n = strlen(v);

	snprintf(full, sizeof full, "user.moob.%s", name);
	buf[0] = 1;
	memcpy(buf + 1, v, n);
	if (fsetxattr(fd, full, buf, n + 1, 0)) die(full);
}

static void set_8(int fd, const char *name, char type, uint64_t bits)
{
	char full[96], buf[9];
	int i;

	snprintf(full, sizeof full, "user.moob.%s", name);
	buf[0] = type;
	for (i = 0; i < 8; i++) buf[1 + i] = (char)(bits >> (8 * i));
	if (fsetxattr(fd, full, buf, 9, 0)) die(full);
}

static void set_bool(int fd, const char *name, int v)
{
	char full[96], buf[2] = { 4, (char)(v != 0) };

	snprintf(full, sizeof full, "user.moob.%s", name);
	if (fsetxattr(fd, full, buf, 2, 0)) die(full);
}

int main(int argc, char **argv)
{
	const char *dir = NULL, *mode = NULL, *sync = "full";
	static const char *kinds[] = { "fact", "rule", "note", "decision" };
	int a, n = 2000, body = 3000, i, dfd, use_x, use_sql;
	uint64_t *t, total = 0, start;
	sqlite3 *db = NULL;
	sqlite3_stmt *ins = NULL;
	char *text, path[4096], tmp[4096], sql[256];

	for (a = 1; a + 1 < argc; a += 2) {
		if (!strcmp(argv[a], "--dir")) dir = argv[a + 1];
		else if (!strcmp(argv[a], "--mode")) mode = argv[a + 1];
		else if (!strcmp(argv[a], "--n")) n = atoi(argv[a + 1]);
		else if (!strcmp(argv[a], "--sync")) sync = argv[a + 1];
		else if (!strcmp(argv[a], "--body")) body = atoi(argv[a + 1]);
		else { fprintf(stderr, "synccost: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || !dir || !mode || n < 10 || body < 1 ||
	    (strcmp(mode, "files") && strcmp(mode, "xattr") && strcmp(mode, "sqlite")) ||
	    (strcmp(sync, "full") && strcmp(sync, "normal"))) {
		fprintf(stderr, "usage: synccost --dir DIR --mode files|xattr|sqlite [--n N] [--sync full|normal] [--body BYTES]\n");
		return 64;
	}
	use_x = !strcmp(mode, "xattr");
	use_sql = !strcmp(mode, "sqlite");
	dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd < 0) die(dir);
	t = calloc((size_t)n, sizeof *t);
	text = malloc((size_t)body);
	if (!t || !text) { errno = ENOMEM; die("alloc"); }
	memset(text, 'm', (size_t)body);
	if (use_sql) {
		snprintf(path, sizeof path, "%s/index.db", dir);
		if (sqlite3_open(path, &db)) { fprintf(stderr, "synccost: %s\n", sqlite3_errmsg(db)); return 1; }
		snprintf(sql, sizeof sql, "PRAGMA journal_mode=WAL; PRAGMA synchronous=%s;", sync);
		if (sqlite3_exec(db, sql, NULL, NULL, NULL) ||
		    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS m(path TEXT PRIMARY KEY, kind TEXT, agent TEXT,"
				     " namespace TEXT, title TEXT, score INTEGER, created INTEGER, weight REAL, pinned INTEGER);"
				     "CREATE INDEX IF NOT EXISTS m_ka ON m(kind, agent);"
				     "CREATE INDEX IF NOT EXISTS m_c ON m(created);", NULL, NULL, NULL) ||
		    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO m VALUES(?,?,?,?,?,?,?,?,?)", -1, &ins, NULL)) {
			fprintf(stderr, "synccost: %s\n", sqlite3_errmsg(db));
			return 1;
		}
	}
	for (i = 0; i < n; i++) {
		char agent[16], title[64];
		const char *kind = kinds[i % 4];
		double weight = (double)(i % 100) / 100.0;
		uint64_t wbits, created = 1760000000000000000ull + (uint64_t)(unsigned int)i * 1000000ull;
		int fd;

		snprintf(agent, sizeof agent, "a%02d", i % 16);
		snprintf(title, sizeof title, "memory number %d", i);
		snprintf(path, sizeof path, "%s/m%06d.md", dir, i);
		snprintf(tmp, sizeof tmp, "%s/.tmp.%d", dir, i);
		memcpy(&wbits, &weight, 8);
		start = now_ns();
		fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0) die(tmp);
		if (write(fd, text, (size_t)body) != body) die("write");
		if (use_x) {
			set_str(fd, "kind", kind);
			set_str(fd, "agent", agent);
			set_str(fd, "namespace", "ns0");
			set_str(fd, "title", title);
			set_8(fd, "score", 2, (uint64_t)(i % 1000));
			set_8(fd, "created", 5, created);
			set_8(fd, "weight", 3, wbits);
			set_bool(fd, "pinned", i % 7 == 0);
		}
		if (fsync(fd)) die("fsync");
		close(fd);
		if (rename(tmp, path)) die("rename");
		if (fsync(dfd)) die("fsync dir");
		if (use_sql) {
			sqlite3_reset(ins);
			sqlite3_bind_text(ins, 1, path, -1, SQLITE_STATIC);
			sqlite3_bind_text(ins, 2, kind, -1, SQLITE_STATIC);
			sqlite3_bind_text(ins, 3, agent, -1, SQLITE_STATIC);
			sqlite3_bind_text(ins, 4, "ns0", -1, SQLITE_STATIC);
			sqlite3_bind_text(ins, 5, title, -1, SQLITE_STATIC);
			sqlite3_bind_int64(ins, 6, i % 1000);
			sqlite3_bind_int64(ins, 7, (sqlite3_int64)created);
			sqlite3_bind_double(ins, 8, weight);
			sqlite3_bind_int(ins, 9, i % 7 == 0);
			if (sqlite3_step(ins) != SQLITE_DONE) { fprintf(stderr, "synccost: %s\n", sqlite3_errmsg(db)); return 1; }
		}
		t[i] = now_ns() - start;
		total += t[i];
	}
	if (ins) sqlite3_finalize(ins);
	if (db) sqlite3_close(db);
	qsort(t, (size_t)n, sizeof *t, cmp);
	printf("mode=%s%s%s n=%d body=%d p50_us=%.1f p95_us=%.1f mean_us=%.1f total_s=%.2f\n", mode,
	       use_sql ? " sync=" : "", use_sql ? sync : "", n, body, (double)t[n / 2] / 1e3,
	       (double)t[n * 95 / 100] / 1e3, (double)total / n / 1e3, (double)total / 1e9);
	return 0;
}
