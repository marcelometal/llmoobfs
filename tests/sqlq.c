/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sqlq: tests of the moobfs SQLite virtual table (sqlite/moobfs_vtab.c, journal 0108), with the
 * extension linked in.
 *
 *   sqlq compare <layer domain> <lower domain> <column>... -- <sql>...
 *       Creates the virtual table vt over the layer's domain and an ordinary table ref filled by
 *       reading the attributes straight from the lower filesystem; runs each statement with {T}
 *       replaced by vt and by ref and compares the rows (in order when the statement has ORDER
 *       BY, as sets otherwise). Prints the query plan of the vt statement, then SQLQ_SAME or the
 *       difference. Exits 1 at the first difference.
 *   sqlq [--uid N] run <layer domain> <column>... -- <sql>
 *       Prints the rows of one statement over vt, one per line, values separated by tabs; with
 *       --uid, as that user (uid and gid N).
 *
 * A column is "<attribute> <type>", the first one the key. Run as root for compare: ref reads
 * every file.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <sqlite3ext.h>
#include "moobfs_attr.h"

int sqlite3_moobfs_init(sqlite3 *db, char **err, const sqlite3_api_routines *api);

static sqlite3 *db;
static char **cols;
static int ncols;
static const char *lower_root;
static sqlite3_stmt *ins;

static void die(const char *what)
{
	fprintf(stderr, "sqlq: %s: %s\n", what, db ? sqlite3_errmsg(db) : "");
	exit(2);
}

/* The name and type of column i ("created time required" -> created, time); returns required. */
static int col_split(int i, char *name, size_t n, const char **type)
{
	const char *sp = strchr(cols[i], ' ');

	snprintf(name, n, "%.*s", (int)(sp - cols[i]), cols[i]);
	*type = sp + 1;
	return i == 0 || strstr(sp + 1, " required") != NULL;
}

static const char *sqltype(const char *t)
{
	if (!strncmp(t, "string", 6)) return "TEXT";
	if (!strncmp(t, "double", 6)) return "REAL";
	if (!strncmp(t, "raw", 3)) return "BLOB";
	return "INTEGER";
}

static void bind_value(int idx, const unsigned char *v, ssize_t n)
{
	if (n < 1 || moob_attr_validate(v, (size_t)n) != MOOB_ATTR_OK) {
		sqlite3_bind_null(ins, idx);
		return;
	}
	switch (v[0]) {
	case MOOB_T_STRING:
		sqlite3_bind_text(ins, idx, (const char *)v + 1, (int)n - 1, SQLITE_TRANSIENT);
		break;
	case MOOB_T_INT64:
	case MOOB_T_TIME:
		sqlite3_bind_int64(ins, idx, (sqlite3_int64)moob_attr_get64(v + 1));
		break;
	case MOOB_T_DOUBLE: {
		ma_u64 bits = moob_attr_get64(v + 1);
		double d;

		memcpy(&d, &bits, 8);
		sqlite3_bind_double(ins, idx, d);
		break;
	}
	case MOOB_T_BOOL:
		sqlite3_bind_int(ins, idx, v[1]);
		break;
	default:
		sqlite3_bind_blob(ins, idx, v + 1, (int)n - 1, SQLITE_TRANSIENT);
	}
}

/* One file of the lower domain: a row of ref when it carries the key. */
static int walk(const char *path, const struct stat *st, int flag, struct FTW *f)
{
	unsigned char v[MOOB_ATTR_VALUE_MAX];
	char name[128], xname[160];
	const char *type;
	ssize_t n;
	int i;

	(void)flag;
	if (f->level == 0 || !(S_ISREG(st->st_mode) || S_ISDIR(st->st_mode)))
		return 0;	/* the domain directory's own attributes belong outside it */
	for (i = 0; i < ncols; i++) {	/* a row carries the key and every required attribute */
		if (!col_split(i, name, sizeof(name), &type))
			continue;
		snprintf(xname, sizeof(xname), "%s%s", MOOB_ATTR_PREFIX, name);
		if (lgetxattr(path, xname, v, sizeof(v)) < 0)
			return 0;
	}
	sqlite3_reset(ins);
	sqlite3_bind_int64(ins, 1, (sqlite3_int64)st->st_ino);
	sqlite3_bind_text(ins, 2, path + strlen(lower_root) + 1, -1, SQLITE_TRANSIENT);
	for (i = 0; i < ncols; i++) {
		col_split(i, name, sizeof(name), &type);
		snprintf(xname, sizeof(xname), "%s%s", MOOB_ATTR_PREFIX, name);
		n = lgetxattr(path, xname, v, sizeof(v));
		if (n < 0)
			sqlite3_bind_null(ins, i + 3);
		else
			bind_value(i + 3, v, n);
	}
	if (sqlite3_step(ins) != SQLITE_DONE)
		die("insert");
	return 0;
}

static void make_vt(const char *domain)
{
	sqlite3_str *s = sqlite3_str_new(db);
	char *sql, *err = NULL;
	int i;

	sqlite3_str_appendf(s, "CREATE VIRTUAL TABLE temp.vt USING moobfs(%Q", domain);
	for (i = 0; i < ncols; i++)
		sqlite3_str_appendf(s, ", %Q", cols[i]);
	sqlite3_str_appendall(s, ")");
	sql = sqlite3_str_finish(s);
	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
		fprintf(stderr, "sqlq: %s: %s\n", sql, err);
		exit(2);
	}
	sqlite3_free(sql);
}

static void make_ref(const char *lower)
{
	sqlite3_str *s = sqlite3_str_new(db), *q = sqlite3_str_new(db);
	char name[128], *sql, *qs;
	const char *type;
	int i;

	sqlite3_str_appendall(s, "CREATE TEMP TABLE ref(rowid INTEGER PRIMARY KEY, path TEXT");
	sqlite3_str_appendall(q, "INSERT INTO ref VALUES(?, ?");
	for (i = 0; i < ncols; i++) {
		col_split(i, name, sizeof(name), &type);
		sqlite3_str_appendf(s, ", \"%w\" %s", name, sqltype(type));
		sqlite3_str_appendall(q, ", ?");
	}
	sqlite3_str_appendall(s, ")");
	sqlite3_str_appendall(q, ")");
	sql = sqlite3_str_finish(s);
	qs = sqlite3_str_finish(q);
	if (sqlite3_exec(db, sql, NULL, NULL, NULL) != SQLITE_OK)
		die(sql);
	if (sqlite3_prepare_v2(db, qs, -1, &ins, NULL) != SQLITE_OK)
		die(qs);
	lower_root = lower;
	if (nftw(lower, walk, 32, FTW_PHYS) < 0)
		die("walk of the lower domain");
	sqlite3_finalize(ins);
	sqlite3_free(sql);
	sqlite3_free(qs);
}

/* sql with every {T} replaced by table. */
static char *subst(const char *sql, const char *table)
{
	sqlite3_str *s = sqlite3_str_new(db);
	const char *p;

	while ((p = strstr(sql, "{T}"))) {
		sqlite3_str_append(s, sql, (int)(p - sql));
		sqlite3_str_appendall(s, table);
		sql = p + 3;
	}
	sqlite3_str_appendall(s, sql);
	return sqlite3_str_finish(s);
}

struct rows {
	char **line;
	int n, cap;
};

static int run(const char *sql, struct rows *r, int print)
{
	sqlite3_stmt *st;
	int rc, i, nc;

	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
		fprintf(stderr, "sqlq: %s: %s\n", sql, sqlite3_errmsg(db));
		return -1;
	}
	nc = sqlite3_column_count(st);
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		sqlite3_str *s = sqlite3_str_new(db);
		char *line;

		for (i = 0; i < nc; i++) {
			const unsigned char *t = sqlite3_column_text(st, i);

			sqlite3_str_appendf(s, "%s%s", i ? "\t" : "", t ? (const char *)t : "NULL");
		}
		line = sqlite3_str_finish(s);
		if (print)
			printf("%s\n", line);
		if (r) {
			if (r->n == r->cap) {
				r->cap = r->cap ? 2 * r->cap : 64;
				r->line = realloc(r->line, sizeof(char *) * (size_t)r->cap);
				if (!r->line)
					die("memory");
			}
			r->line[r->n++] = line;
		} else {
			sqlite3_free(line);
		}
	}
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "sqlq: %s: %s\n", sql, sqlite3_errmsg(db));
		sqlite3_finalize(st);
		return -1;
	}
	sqlite3_finalize(st);
	return 0;
}

static int cmp_line(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void plan(const char *sql)
{
	char *e = sqlite3_mprintf("EXPLAIN QUERY PLAN %s", sql);
	sqlite3_stmt *st;

	if (sqlite3_prepare_v2(db, e, -1, &st, NULL) == SQLITE_OK) {
		while (sqlite3_step(st) == SQLITE_ROW)
			printf("  plan: %s\n", sqlite3_column_text(st, 3));
		sqlite3_finalize(st);
	}
	sqlite3_free(e);
}

static int compare(const char *sql)
{
	char *a = subst(sql, "vt"), *b = subst(sql, "ref");
	struct rows ra = { 0 }, rb = { 0 };
	int i, ordered = strstr(sql, "ORDER BY") != NULL, same;

	printf("query: %s\n", sql);
	plan(a);
	if (run(a, &ra, 0) || run(b, &rb, 0))
		return 1;
	if (!ordered) {
		qsort(ra.line, (size_t)ra.n, sizeof(char *), cmp_line);
		qsort(rb.line, (size_t)rb.n, sizeof(char *), cmp_line);
	}
	same = ra.n == rb.n;
	for (i = 0; same && i < ra.n; i++)
		same = !strcmp(ra.line[i], rb.line[i]);
	if (same) {
		printf("SQLQ_SAME rows=%d\n", ra.n);
	} else {
		printf("SQLQ_DIFFER vt=%d ref=%d\n", ra.n, rb.n);
		for (i = 0; i < ra.n || i < rb.n; i++)
			printf("  %-40s | %s\n", i < ra.n ? ra.line[i] : "-", i < rb.n ? rb.line[i] : "-");
	}
	sqlite3_free(a);
	sqlite3_free(b);
	return !same;
}

int main(int argc, char **argv)
{
	int k = 1, i, sep, uid = -1;

	if (argc > 3 && !strcmp(argv[1], "--uid")) {
		uid = atoi(argv[2]);
		k = 3;
	}
	if (argc - k < 4)
		goto usage;
	for (sep = k; sep < argc && strcmp(argv[sep], "--"); sep++)
		;
	if (sep >= argc - 1)
		goto usage;
	if (sqlite3_open(":memory:", &db) != SQLITE_OK || sqlite3_moobfs_init(db, NULL, NULL) != SQLITE_OK)
		die("open");
	if (!strcmp(argv[k], "compare")) {
		cols = &argv[k + 3];
		ncols = sep - (k + 3);
		if (ncols < 1)
			goto usage;
		make_ref(argv[k + 2]);
		make_vt(argv[k + 1]);
		for (i = sep + 1; i < argc; i++)
			if (compare(argv[i]))
				return 1;
		printf("SQLQ_COMPARE_DONE\n");
		return 0;
	}
	if (!strcmp(argv[k], "run")) {
		cols = &argv[k + 2];
		ncols = sep - (k + 2);
		if (ncols < 1)
			goto usage;
		if (uid >= 0 && (setresgid((gid_t)uid, (gid_t)uid, (gid_t)uid) ||
				 setresuid((uid_t)uid, (uid_t)uid, (uid_t)uid)))
			die("drop privileges");
		make_vt(argv[k + 1]);
		return run(argv[sep + 1], NULL, 1) ? 1 : 0;
	}
usage:
	fprintf(stderr, "usage: sqlq compare <layer domain> <lower domain> <col>... -- <sql>...\n"
			"       sqlq [--uid N] run <layer domain> <col>... -- <sql>\n");
	return 2;
}
