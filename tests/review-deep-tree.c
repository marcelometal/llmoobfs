/* SPDX-License-Identifier: GPL-2.0-only */
/* Review of 3627172 (journal 0153), finding R2-1: a directory 65 or more components below the
 * mount root is not placed by where_is(); its permission changes set no fallback and a
 * value change of a file below it is not indexed. Disposable VM only; a fresh domain is required. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static void die(const char *what) { perror(what); exit(2); }

static unsigned int query(int fd, const char *eq, char *first)
{
	struct moob_query_result r[8];
	struct moob_query2 q = {0};

	q.version = MOOB_QUERY2_VERSION; q.nterms = 1; q.limit = 8;
	q.results = (uint64_t)(uintptr_t)r;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = eq ? MOOB_OP_EQ : MOOB_OP_EXISTS;
	if (eq) { q.terms[0].type = 1; q.terms[0].len = (uint16_t)strlen(eq); memcpy(q.terms[0].value, eq, strlen(eq)); }
	if (ioctl(fd, MOOB_IOC_QUERY2, &q)) die("QUERY2");
	strcpy(first, q.count ? r[0].path : "-");
	return q.count;
}

int main(int argc, char **argv)
{
	char path[4096], dirk[4096] = "", first[300], v[8];
	int n, k, i, fd, start[2], go[2], status;
	pid_t pid;

	if (argc != 4) { fprintf(stderr, "usage: deep <fresh domain> <depth> <chmod-depth|0>\n"); return 2; }
	setbuf(stdout, NULL);
	n = atoi(argv[2]); k = atoi(argv[3]);
	umask(0);
	if (mkdir(argv[1], 0755)) die("mkdir domain");
	snprintf(path, sizeof path, "%s", argv[1]);
	for (i = 1; i <= n; i++) {
		strcat(path, "/a");
		if (mkdir(path, 0755)) die("mkdir chain");
		if (i == k) strcpy(dirk, path);
	}
	strcat(path, "/f");
	fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
	if (fd < 0)
		die("create");
	close(fd);
	if (setxattr(path, "user.moob.kind", "\1x", 2, 0)) die("setxattr x");
	if (pipe(start) || pipe(go)) die("pipe");
	pid = fork();
	if (pid < 0) die("fork");
	if (!pid) {
		unsigned int c0, c1, c2, c3; int rc, e;
		close(start[0]); close(go[1]);
		if (setgroups(0, NULL) || setgid(1001) || setuid(1001)) die("drop identity");
		fd = open(argv[1], O_RDONLY | O_DIRECTORY);
		if (fd < 0) die("open domain");
		c0 = query(fd, NULL, first);
		printf("BEFORE exists=%u first=%s\n", c0, first);
		if (write(start[1], "x", 1) != 1) die("signal");
		if (read(go[0], v, 1) != 1) die("wait go");
		c1 = query(fd, NULL, first);
		c2 = query(fd, "x", first);
		c3 = query(fd, "y", first);
		rc = open(path, O_RDONLY); e = errno;
		printf("REVIEW2_RESULT depth=%d chmod_depth=%d uid=%u before=%u exists=%u eq_x=%u eq_y=%u open=%d errno=%d first=%s\n",
		       n, k, (unsigned)getuid(), c0, c1, c2, c3, rc < 0 ? -1 : 0, rc < 0 ? e : 0, first);
		_exit(0);
	}
	close(start[1]); close(go[0]);
	if (read(start[0], v, 1) != 1) die("wait start");
	if (k && chmod(dirk, 0700)) die("chmod");
	if (setxattr(path, "user.moob.kind", "\1y", 2, 0)) die("setxattr y");
	if (write(go[1], "x", 1) != 1) die("go");
	if (waitpid(pid, &status, 0) != pid) die("waitpid");
	return 0;
}
