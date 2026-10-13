/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * udx-query: answers a query from the user-space baseline index of one domain
 * (docs/history/0092). Same terms, options and output as "moobfs-index --domain", so the two
 * can be compared byte for byte; it opens only "<domain dir>/.udx.db" and touches no file.
 *
 *   udx-query [--require-live] [--order NAME TYPE asc|desc] [--limit N] <domain dir> [terms...]
 *   udx-query --status <domain dir>
 *   udx-query --sql ... <domain dir> [terms...]     prints the statement instead of running it
 *
 * Terms: <name> <op> <type> <value>, up to four, ops eq lt le gt ge prefix exists, "exists"
 * takes "- -", an op ending in '?' also matches entries without the attribute
 * (tools/query-common.h). Output: "<ino>\t<path>" sorted by inode, or by (value, ino) with
 * --order. --domain is accepted and ignored. --require-live exits 75 unless udx-watch holds
 * this domain's lock and the last complete pass was not followed by a known loss of events;
 * without it the answer is whatever the database holds, however old.
 */
#define _GNU_SOURCE
#include <sys/file.h>
#include <inttypes.h>
#include <stdarg.h>
#include "udx.h"
#include "query-common.h"

struct bind { int type; int64_t k; const void *p; size_t n; };	/* type 0: text name */
static struct bind binds[64];
static int nbinds;
static char sql[4096];
static size_t sqllen;
static ma_u8 uppers[MOOB_QUERY_MAX_TERMS][MOOB_QUERY_VALUE_MAX];

static void add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void add(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(sql + sqllen, sizeof sql - sqllen, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof sql - sqllen)
		exit(70);
	sqllen += (size_t)n;
}

static int bind_name(const char *name)
{
	binds[nbinds] = (struct bind){ 0, 0, name, strlen(name) };
	return ++nbinds;
}

static int bind_int(int64_t k)
{
	binds[nbinds] = (struct bind){ 1, k, NULL, 0 };
	return ++nbinds;
}

static int bind_blob(const void *p, size_t n)
{
	binds[nbinds] = (struct bind){ 2, 0, p, n };
	return ++nbinds;
}

/* The value of a term in the representation of attr.v. */
static int bind_value(const struct moob_query_term *t)
{
	switch (t->type) {
	case MOOB_T_STRING: return bind_blob(t->value, t->len);
	case MOOB_T_DOUBLE: return bind_int(udx_double_key(moob_attr_get64(t->value)));
	case MOOB_T_BOOL: return bind_int(t->value[0]);
	default: return bind_int((int64_t)moob_attr_get64(t->value));
	}
}

/* Appends " AND <alias>.name = ? [AND type = ? AND v <op> ?]": what term i asks of a row. */
static void condition(const char *alias, const struct moob_query_term *t, int i)
{
	static const char *const cmp[] = { [MOOB_OP_EQ] = "=", [MOOB_OP_LT] = "<", [MOOB_OP_LE] = "<=",
					   [MOOB_OP_GT] = ">", [MOOB_OP_GE] = ">=" };

	add(" AND %s.name = ?%d", alias, bind_name(t->name));
	if (t->op == MOOB_OP_EXISTS)
		return;
	add(" AND %s.type = ?%d", alias, bind_int(t->type));
	if (t->op != MOOB_OP_PREFIX) {
		add(" AND %s.v %s ?%d", alias, cmp[t->op], bind_value(t));
		return;
	}
	if (t->type != MOOB_T_STRING) {
		/* the oracle compares the payload bytes; a fixed-size payload makes that equality */
		add(" AND %s.v = ?%d", alias, bind_value(t));
		return;
	}
	if (t->len) {
		size_t n = t->len;

		/* a prefix is the range [p, successor of p): the index answers it directly */
		add(" AND %s.v >= ?%d", alias, bind_blob(t->value, t->len));
		memcpy(uppers[i], t->value, n);
		while (n && uppers[i][n - 1] == 0xff)
			n--;
		if (n) {
			uppers[i][n - 1]++;
			add(" AND %s.v < ?%d", alias, bind_blob(uppers[i], n));
		}
	}
}

static int status(const char *dir, int quiet)
{
	char path[PATH_MAX], state[32] = "?";
	sqlite3 *db;
	sqlite3_stmt *st;
	long long overflows = -1, synced = 0;
	int fd, watched, persist = 1;

	snprintf(path, sizeof path, "%s/%s", dir, UDX_DB_NAME);
	fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	/* udx-watch holds an exclusive flock() on every database it maintains */
	watched = flock(fd, LOCK_SH | LOCK_NB) != 0;
	close(fd);	/* before SQLite opens the file: closing a descriptor drops its POSIX locks */
	udx_sqlite_init();
	if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW, NULL) != SQLITE_OK ||
	    sqlite3_file_control(db, NULL, SQLITE_FCNTL_PERSIST_WAL, &persist) != SQLITE_OK ||
	    sqlite3_prepare_v2(db, "SELECT key, value FROM meta", -1, &st, NULL) != SQLITE_OK) {
		fprintf(stderr, "udx-query: %s: %s\n", path, sqlite3_errmsg(db));
		return 1;
	}
	sqlite3_busy_timeout(db, 10000);
	while (sqlite3_step(st) == SQLITE_ROW) {
		const char *k = (const char *)sqlite3_column_text(st, 0);

		if (!strcmp(k, "state"))
			snprintf(state, sizeof state, "%s", (const char *)sqlite3_column_text(st, 1));
		else if (!strcmp(k, "overflows"))
			overflows = sqlite3_column_int64(st, 1);
		else if (!strcmp(k, "synced_ns"))
			synced = sqlite3_column_int64(st, 1);
	}
	sqlite3_finalize(st);
	sqlite3_close(db);
	if (!quiet)
		printf("watcher=%s state=%s synced_ns=%lld overflows=%lld\n", watched ? "running" : "absent",
		       state, synced, overflows);
	return watched && !strcmp(state, "synced") ? 0 : 75;
}

int main(int argc, char **argv)
{
	struct moob_query_term terms[MOOB_QUERY_MAX_TERMS];
	char order_name[MOOB_QUERY_NAME_MAX + 1] = "", path[PATH_MAX];
	int order_type = 0, order_desc = 0, nterms, i, rc, require_live = 0, show_sql = 0;
	long limit = -1;
	sqlite3 *db;
	sqlite3_stmt *st;

	for (;;) {
		if (argc >= 3 && !strcmp(argv[1], "--status")) {
			return status(argv[2], 0);
		} else if (argc >= 2 && !strcmp(argv[1], "--domain")) {
			argv++; argc--;
		} else if (argc >= 2 && !strcmp(argv[1], "--require-live")) {
			require_live = 1;
			argv++; argc--;
		} else if (argc >= 2 && !strcmp(argv[1], "--sql")) {
			show_sql = 1;
			argv++; argc--;
		} else if (argc >= 5 && !strcmp(argv[1], "--order")) {
			size_t olen = strlen(argv[2]);

			order_type = parse_type(argv[3]);
			if (order_type < 0 || olen >= sizeof order_name) return 64;
			memcpy(order_name, argv[2], olen + 1);
			order_desc = !strcmp(argv[4], "desc");
			argv += 4; argc -= 4;
		} else if (argc >= 3 && !strcmp(argv[1], "--limit")) {
			limit = strtol(argv[2], NULL, 10);
			argv += 2; argc -= 2;
		} else {
			break;
		}
	}
	if (argc < (order_name[0] ? 2 : 6)) {
		fprintf(stderr, "usage: udx-query [--require-live] [--order NAME TYPE asc|desc] [--limit N] <domain dir> [terms...]\n"
				"       udx-query --status <domain dir>\n");
		return 64;
	}
	nterms = parse_terms(argc, argv, 2, terms);
	if (nterms < (order_name[0] ? 0 : 1)) {
		fprintf(stderr, "udx-query: invalid query terms\n");
		return 65;
	}

	/* positive terms and the order attribute are joins on attr's (eid, name) key, one row
	 * each, so SQLite is free to start from whichever index range is smallest; a "missing or"
	 * term cannot drive the scan and stays a filter */
	add("SELECT e.ino, e.path FROM entry e");
	if (order_name[0]) {
		add(" JOIN attr o ON o.eid = e.id AND o.name = ?%d", bind_name(order_name));
		add(" AND o.type = ?%d", bind_int(order_type));
	}
	for (i = 0; i < nterms; i++) {
		char alias[16];

		if (terms[i].flags & MOOB_TERM_MISSING_OK)
			continue;
		snprintf(alias, sizeof alias, "a%d", i);
		add(" JOIN attr %s ON %s.eid = e.id", alias, alias);
		condition(alias, &terms[i], i);
	}
	add(" WHERE e.ok = 1");
	for (i = 0; i < nterms; i++) {
		if (!(terms[i].flags & MOOB_TERM_MISSING_OK))
			continue;
		/* missing means no indexable value under that name; a value of another type is not missing */
		add(" AND (NOT EXISTS (SELECT 1 FROM attr m WHERE m.eid = e.id AND m.name = ?%d)", bind_name(terms[i].name));
		add(" OR EXISTS (SELECT 1 FROM attr m WHERE m.eid = e.id");
		condition("m", &terms[i], i);
		add("))");
	}
	if (order_name[0])
		add(" ORDER BY o.v %s, e.ino %s", order_desc ? "DESC" : "ASC", order_desc ? "DESC" : "ASC");
	else
		add(" ORDER BY e.ino");
	add(" LIMIT ?%d", bind_int(limit < 0 ? -1 : limit));
	if (show_sql) {
		puts(sql);
		return 0;
	}

	if (require_live && (rc = status(argv[1], 1)) != 0) {
		if (rc == 75)
			fprintf(stderr, "udx-query: %s: index is not live (see --status)\n", argv[1]);
		return rc;
	}
	snprintf(path, sizeof path, "%s/%s", argv[1], UDX_DB_NAME);
	udx_sqlite_init();	/* a reader must not wake the watcher either */
	i = 1;
	if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW, NULL) != SQLITE_OK ||
	    sqlite3_file_control(db, NULL, SQLITE_FCNTL_PERSIST_WAL, &i) != SQLITE_OK) {
		fprintf(stderr, "udx-query: %s: %s\n", path, sqlite3_errmsg(db));
		return 1;
	}
	sqlite3_busy_timeout(db, 10000);
	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
		fprintf(stderr, "udx-query: %s: %s\n", path, sqlite3_errmsg(db));
		return 1;
	}
	for (i = 0; i < nbinds; i++) {
		if (binds[i].type == 0)
			sqlite3_bind_text(st, i + 1, binds[i].p, (int)binds[i].n, SQLITE_STATIC);
		else if (binds[i].type == 1)
			sqlite3_bind_int64(st, i + 1, binds[i].k);
		else
			sqlite3_bind_blob(st, i + 1, binds[i].n ? binds[i].p : "", (int)binds[i].n, SQLITE_STATIC);
	}
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		printf("%" PRIu64 "\t", (uint64_t)sqlite3_column_int64(st, 0));
		fwrite(sqlite3_column_blob(st, 1), 1, (size_t)sqlite3_column_bytes(st, 1), stdout);
		putchar('\n');
	}
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "udx-query: %s: %s\n", path, sqlite3_errmsg(db));
		return 1;
	}
	sqlite3_finalize(st);
	sqlite3_close(db);
	return 0;
}
