/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * overbudget-race: a build over the index budget must release what was queried least recently
 * and try again, even while other domains are being built (issue #111, R2-7 of journal 0155).
 * domain_ensure() used one mount-wide flag, set by charge() when it refuses and cleared at the
 * start of every build: a build of another domain could clear it between the refusal and the
 * check, and the first build then failed with ENOMEM instead of releasing a domain.
 *
 *   overbudget-race plant <dir> <files>          creates <files> files in <dir> on the lower
 *                                                filesystem, user.moob.kind a 400-byte string
 *   overbudget-race used <layer mount> <domain>  QUERY2 on <domain>, then prints index_used
 *   overbudget-race run <layer mount> <seconds> <churners>
 *
 * run: domains x and y each fit the budget alone and not together. One process queries x and y
 * in turn, so that every query builds its domain over the budget and releases the other. Each
 * churner renames a directory inside its own domain z<k>, which has no attribute, and queries
 * it, so that z<k> is built again and again: each build cleared the old flag, and none charges
 * the budget or releases a domain. No query of the alternating process may fail: each of x and
 * y fits alone, and the other is idle and releasable by it alone. Prints OVERBUDGET_RACE_PASS with the counts, or
 * OVERBUDGET_RACE_FAIL and exits 1. EAGAIN (renames kept coming) is counted, not a failure.
 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

static struct moob_query_result res[MOOB_QUERY_MAX_LIMIT];

static int q2(int fd)
{
	struct moob_query2 q;

	memset(&q, 0, sizeof q);
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = MOOB_QUERY_MAX_LIMIT;
	q.results = (__u64)(uintptr_t)res;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	return ioctl(fd, MOOB_IOC_QUERY2, &q) ? errno : 0;
}

static int open_dir(const char *mnt, const char *name)
{
	char path[4096];

	snprintf(path, sizeof path, "%s/%s", mnt, name);
	return open(path, O_RDONLY | O_DIRECTORY);
}

static int plant(const char *dir, int files)
{
	char path[4096], v[401];
	int i, fd;

	/* a long value: each entry costs the budget about 400 bytes, so few files fill it */
	memset(v, 'k', sizeof v);
	v[0] = MOOB_T_STRING;

	for (i = 0; i < files; i++) {
		snprintf(path, sizeof path, "%s/f%05d", dir, i);
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0 || close(fd) || setxattr(path, "user.moob.kind", v, sizeof v, 0)) {
			perror(path);
			return 1;
		}
	}
	return 0;
}

static int used(const char *mnt, const char *name)
{
	struct moob_info info;
	int root = open(mnt, O_RDONLY | O_DIRECTORY), dom = open_dir(mnt, name);

	if (root < 0 || dom < 0 || q2(dom) || ioctl(root, MOOB_IOC_GET_INFO, &info)) {
		perror("used");
		return 1;
	}
	printf("%llu\n", (unsigned long long)info.index_used);
	return 0;
}

/* Renames z<k>/s to z<k>/t and back, querying z<k> after each: every query builds z<k> again. */
static void churn(const char *mnt, int k, double end)
{
	char name[16], from[4096], to[4096];
	struct timespec ts;
	int fd, i = 0;

	snprintf(name, sizeof name, "z%d", k);
	fd = open_dir(mnt, name);
	if (fd < 0)
		_exit(2);
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &ts);
		if (ts.tv_sec + ts.tv_nsec / 1e9 >= end)
			_exit(0);
		snprintf(from, sizeof from, "%s/%s/%s", mnt, name, i & 1 ? "t" : "s");
		snprintf(to, sizeof to, "%s/%s/%s", mnt, name, i & 1 ? "s" : "t");
		if (rename(from, to))
			_exit(3);
		q2(fd);	/* ENOMEM or EAGAIN here is not what is tested */
		i++;
	}
}

static int run(const char *mnt, int seconds, int churners)
{
	unsigned long turns = 0, again = 0, nomem = 0, other = 0;
	int x = open_dir(mnt, "x"), y = open_dir(mnt, "y"), k, last = 0;
	struct timespec ts;
	double end;
	pid_t pid[16];

	if (x < 0 || y < 0 || churners < 1 || churners > 16) {
		perror("run");
		return 2;
	}
	clock_gettime(CLOCK_MONOTONIC, &ts);
	end = ts.tv_sec + ts.tv_nsec / 1e9 + seconds;
	for (k = 0; k < churners; k++) {
		pid[k] = fork();
		if (!pid[k])
			churn(mnt, k, end);
	}
	for (;;) {
		int err;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		if (ts.tv_sec + ts.tv_nsec / 1e9 >= end)
			break;
		err = q2(turns & 1 ? y : x);
		turns++;
		if (err == EAGAIN)
			again++;
		else if (err == ENOMEM)
			nomem++;
		else if (err)
			other++, last = err;
	}
	for (k = 0; k < churners; k++) {
		int st;

		if (waitpid(pid[k], &st, 0) != pid[k] || !WIFEXITED(st) || WEXITSTATUS(st)) {
			printf("OVERBUDGET_RACE_FAIL: churner %d ended with status %d\n", k, st);
			return 1;
		}
	}
	printf("overbudget-race: %lu queries of x and y, %lu EAGAIN, %lu ENOMEM, %lu other (last %d)\n",
	       turns, again, nomem, other, last);
	if (nomem || other || turns < 20) {
		printf("OVERBUDGET_RACE_FAIL: a domain that fits alone was refused\n");
		return 1;
	}
	printf("OVERBUDGET_RACE_PASS: every query of a domain that fits alone was answered\n");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 4 && !strcmp(argv[1], "plant"))
		return plant(argv[2], atoi(argv[3]));
	if (argc == 4 && !strcmp(argv[1], "used"))
		return used(argv[2], argv[3]);
	if (argc == 5 && !strcmp(argv[1], "run"))
		return run(argv[2], atoi(argv[3]), atoi(argv[4]));
	fprintf(stderr, "usage: overbudget-race plant <dir> <n> | used <mnt> <dom> | run <mnt> <s> <k>\n");
	return 2;
}
