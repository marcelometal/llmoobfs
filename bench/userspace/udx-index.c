/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * udx-index: builds the user-space baseline index (docs/history/0092). For every directory at
 * depth N below <root> (N = 0: the root itself) it creates or brings up to date
 * "<domain>/.udx.db", a SQLite database of the entries below that domain and their valid typed
 * attributes. The schema and the rules are in udx.h. Running it again is safe and writes only
 * what changed. udx-watch keeps the databases current afterwards.
 *
 *   udx-index <root> <domain depth>
 */
#define _GNU_SOURCE
#include "udx.h"

static unsigned long ndomains, nentries, nattrs, nfailed;

static sqlite3_int64 count(sqlite3 *db, const char *sql)
{
	sqlite3_stmt *st;
	sqlite3_int64 n = 0;

	if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
		udx_die(sql, db);
	if (sqlite3_step(st) == SQLITE_ROW)
		n = sqlite3_column_int64(st, 0);
	sqlite3_finalize(st);
	return n;
}

static void index_domain(const char *dir)
{
	struct udx_db d;

	if (udx_open(&d, dir, 1)) {
		fprintf(stderr, "udx-index: %s: no database\n", dir);
		nfailed++;
		return;
	}
	udx_exec(d.db, "BEGIN IMMEDIATE");
	udx_sync_all(&d);
	udx_meta_text(&d, "state", "synced");
	udx_meta_int(&d, "synced_ns", udx_now_ns());
	udx_exec(d.db, "COMMIT");
	udx_exec(d.db, "ANALYZE");
	ndomains++;
	nentries += (unsigned long)count(d.db, "SELECT count(*) FROM entry");
	nattrs += (unsigned long)count(d.db, "SELECT count(*) FROM attr");
	udx_close(&d);
}

static void descend(char *path, size_t len, int depth)
{
	struct dirent *e;
	DIR *dp;

	if (!depth) {
		index_domain(path);
		return;
	}
	dp = opendir(path);
	if (!dp)
		return;
	while ((e = readdir(dp))) {
		size_t nl = strlen(e->d_name);
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || len + 1 + nl >= PATH_MAX)
			continue;
		path[len] = '/';
		memcpy(path + len + 1, e->d_name, nl + 1);
		if (!lstat(path, &st) && S_ISDIR(st.st_mode))
			descend(path, len + 1 + nl, depth - 1);
	}
	path[len] = 0;
	closedir(dp);
}

int main(int argc, char **argv)
{
	char path[PATH_MAX];
	char *end;
	long depth;
	size_t len;

	if (argc != 3) {
		fprintf(stderr, "usage: udx-index <root> <domain depth>\n");
		return 64;
	}
	depth = strtol(argv[2], &end, 10);
	len = strlen(argv[1]);
	if (*end || depth < 0 || depth > 64 || !len || len >= PATH_MAX)
		return 64;
	memcpy(path, argv[1], len + 1);
	while (len > 1 && path[len - 1] == '/')
		path[--len] = 0;
	descend(path, len, (int)depth);
	printf("udx-index: %lu domains, %lu entries, %lu attribute rows, %lu rows written\n",
	       ndomains, nentries, nattrs, udx_rows_written);
	return nfailed || !ndomains;
}
