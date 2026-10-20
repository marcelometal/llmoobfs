/* SPDX-License-Identifier: GPL-2.0-only */
/* Review of 3627172 (journal 0153), finding R2-2: a same-directory rename during the scan's read of the
 * file leaves the scan's listing name in the record. Disposable VM only; fresh domain. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static void die(const char *what) { perror(what); exit(2); }

static unsigned int query(int fd, char *first)
{
	struct moob_query_result r[8];
	struct moob_query2 q = {0};

	q.version = MOOB_QUERY2_VERSION; q.nterms = 1; q.limit = 8;
	q.results = (uint64_t)(uintptr_t)r;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (ioctl(fd, MOOB_IOC_QUERY2, &q)) die("QUERY2");
	snprintf(first, 300, "%s%s", q.count ? r[0].path : "-",
		 q.count && (r[0].flags & MOOB_RES_PATH_UNKNOWN) ? "(unknown)" : "");
	return q.count;
}

static void drop_caches(void)
{
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	if (fd < 0 || write(fd, "2", 1) != 1)
		die("drop_caches");
	close(fd);
}

int main(int argc, char **argv)
{
	char a[1024], b[1024], first[300], v;
	int fd, rename_delay_us, status, start[2];
	unsigned int c0, c1, c2;
	pid_t pid;

	if (argc != 3) { fprintf(stderr, "usage: samedir <fresh domain> <rename delay us|-1 for none>\n"); return 2; }
	setbuf(stdout, NULL);
	rename_delay_us = atoi(argv[2]);
	umask(0);
	if (mkdir(argv[1], 0755)) die("mkdir domain");
	snprintf(a, sizeof a, "%s/a", argv[1]);
	snprintf(b, sizeof b, "%s/b", argv[1]);
	fd = open(a, O_CREAT | O_EXCL | O_RDWR, 0644);
	if (fd < 0)
		die("create");
	close(fd);
	if (setxattr(a, "user.moob.kind", "\1x", 2, 0)) die("setxattr");
	if (pipe(start)) die("pipe");
	pid = fork();
	if (pid < 0) die("fork");
	if (!pid) {
		close(start[0]);
		fd = open(argv[1], O_RDONLY | O_DIRECTORY);
		if (fd < 0) die("open domain");
		if (write(start[1], "x", 1) != 1) die("signal");
		c0 = query(fd, first);	/* the first query: builds the domain, sleeps scan_delay_us per file */
		printf("BUILD_QUERY count=%u first=%s\n", c0, first);
		_exit(0);
	}
	close(start[1]);
	if (read(start[0], &v, 1) != 1) die("wait start");
	if (rename_delay_us >= 0) {
		usleep((unsigned)rename_delay_us);
		if (rename(a, b)) die("rename");
		printf("RENAMED a->b during the build\n");
	}
	if (waitpid(pid, &status, 0) != pid) die("waitpid");
	if (rename_delay_us < 0 && rename(a, b)) die("rename after");
	fd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (fd < 0) die("open domain");
	c1 = query(fd, first);
	printf("CACHED count=%u first=%s\n", c1, first);
	drop_caches();
	c2 = query(fd, first);
	printf("REVIEW2_RESULT samedir delay=%d cached=%u after_drop=%u path_after_drop=%s exists_b=%d exists_a=%d\n",
	       rename_delay_us, c1, c2, first, access(b, F_OK) == 0, access(a, F_OK) == 0);
	return 0;
}
