/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bench/sqlclient: the W3 client of baselines A and B. Executes one prepared SQL
 * statement per line of a file (produced by bench/sqlbase.sh sql) against the
 * baseline database and records one latency record per statement, exactly as
 * bench/load records one per QUERY ioctl: workload 3, op QUERY, result = row count.
 * Linked with the system SQLite library so the measured path is the library the
 * application would use, not the sqlite3 shell.
 *
 *   sqlclient --db FILE --sql FILE --out PREFIX [--repeat N]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sqlite3.h>
#include "common.h"

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int main(int argc, char **argv)
{
	const char *db = NULL, *sqlf = NULL, *out = NULL;
	int a, repeat = 1, r, rc = 0;
	sqlite3 *h;
	FILE *recf;
	char recname[4096];

	for (a = 1; a + 1 < argc; a += 2) {
		if (!strcmp(argv[a], "--db")) db = argv[a + 1];
		else if (!strcmp(argv[a], "--sql")) sqlf = argv[a + 1];
		else if (!strcmp(argv[a], "--out")) out = argv[a + 1];
		else if (!strcmp(argv[a], "--repeat")) repeat = atoi(argv[a + 1]);
		else { fprintf(stderr, "sqlclient: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || !db || !sqlf || !out || repeat < 1) {
		fprintf(stderr, "usage: sqlclient --db FILE --sql FILE --out PREFIX [--repeat N]\n");
		return 64;
	}
	if (sqlite3_open_v2(db, &h, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
		fprintf(stderr, "sqlclient: %s: %s\n", db, sqlite3_errmsg(h));
		return 1;
	}
	snprintf(recname, sizeof recname, "%s.0.rec", out);
	recf = bench_create_out(recname);
	if (!recf) { perror(recname); return 1; }
	for (r = 0; r < repeat; r++) {
		FILE *f = fopen(sqlf, "r");
		char *line = NULL;
		size_t cap = 0;

		if (!f) { perror(sqlf); rc = 1; break; }
		int shape = 0;

		while (getline(&line, &cap, f) > 0) {
			sqlite3_stmt *st;
			struct bench_rec rec = { .workload = 3, .proc = 0 };
			int32_t count = 0;
			int step;

			if (line[0] == '#' || line[0] == '\n') continue;
			rec.op = BENCH_QUERY_OP(++shape);
			rec.start_ns = now_ns();
			if (sqlite3_prepare_v2(h, line, -1, &st, NULL) != SQLITE_OK) {
				rec.result = -EINVAL;
				fprintf(stderr, "sqlclient: prepare: %s: %s", sqlite3_errmsg(h), line);
				rc = 1;
			} else {
				while ((step = sqlite3_step(st)) == SQLITE_ROW) count++;
				rec.result = step == SQLITE_DONE ? count : -EIO;
				if (step != SQLITE_DONE) rc = 1;
				sqlite3_finalize(st);
			}
			rec.dur_ns = now_ns() - rec.start_ns;
			fwrite(&rec, sizeof rec, 1, recf);
		}
		free(line);
		fclose(f);
	}
	fclose(recf);
	sqlite3_close(h);
	return rc;
}
