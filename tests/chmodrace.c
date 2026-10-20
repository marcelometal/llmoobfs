/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A chmod that lands while the first build of a domain is reading the file (journals 0151,
 * 0152). A child running as uid 1001 starts the build with a query; the parent makes the file
 * private (0600) and writes a new value while the scan sleeps (scan_delay_us). The file must
 * then be absent from the domain's answers, as it is when the chmod comes after the build.
 * Exit 0: absent; 1: still answered to a caller that cannot read it; 3: the setup did not
 * produce the revoked state (getxattr did not fail with EACCES); 2: error.
 * Disposable VM only; the domain directory must not exist yet.
 */
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

static void die(const char *what)
{
	perror(what);
	exit(2);
}

static unsigned int query(int fd, int equality)
{
	struct moob_query_result result[8];
	struct moob_query2 q = {0};

	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 8;
	q.results = (uint64_t)(uintptr_t)result;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = equality ? MOOB_OP_EQ : MOOB_OP_EXISTS;
	if (equality) {
		q.terms[0].type = 1;
		q.terms[0].len = 3;
		memcpy(q.terms[0].value, "new", 3);
	}
	if (ioctl(fd, MOOB_IOC_QUERY2, &q))
		die("QUERY2");
	printf("QUERY equality=%d count=%u first=%s\n", equality, q.count,
	       q.count ? result[0].path : "-");
	return q.count;
}

int main(int argc, char **argv)
{
	char path[1024], value[32];
	int fd, start[2], status;
	pid_t pid;
	unsigned int delay;

	if (argc != 3) {
		fprintf(stderr, "usage: chmodrace fresh-domain chmod-delay-us\n");
		return 2;
	}
	setbuf(stdout, NULL);
	delay = (unsigned int)strtoul(argv[2], NULL, 10);
	umask(0);
	if (mkdir(argv[1], 0755))
		die("mkdir fresh domain");
	snprintf(path, sizeof(path), "%s/f", argv[1]);
	fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0644);
	if (fd < 0)
		die("create");
	close(fd);
	if (setxattr(path, "user.moob.kind", "\1old", 4, 0))
		die("set old value");
	if (pipe(start))
		die("pipe");
	pid = fork();
	if (pid < 0)
		die("fork");
	if (!pid) {
		unsigned int first, after;
		int rc, saved;

		close(start[0]);
		if (setgroups(0, NULL) || setgid(1001) || setuid(1001))
			die("drop identity");
		fd = open(argv[1], O_RDONLY | O_DIRECTORY);
		if (fd < 0)
			die("open domain");
		if (write(start[1], "x", 1) != 1)
			die("signal");
		first = query(fd, 0);
		/* In the prebuilt control, let the parent revoke after the first query. */
		usleep(delay + 200000);
		rc = getxattr(path, "user.moob.kind", value, sizeof(value));
		saved = errno;
		after = query(fd, 1);
		printf("REVIEW_RESULT uid=%u first=%u after=%u getxattr=%d errno=%d\n",
		       (unsigned int)getuid(), first, after, rc, saved);
		close(fd);
		if (rc != -1 || saved != EACCES)
			_exit(3);
		_exit(after ? 1 : 0);
	}
	close(start[1]);
	if (read(start[0], value, 1) != 1)
		die("wait start");
	usleep(delay);
	if (chmod(path, 0600))
		die("chmod private");
	if (setxattr(path, "user.moob.kind", "\1new", 4, 0))
		die("set private value");
	printf("REVOKED_AND_UPDATED\n");
	if (waitpid(pid, &status, 0) != pid)
		die("waitpid");
	return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}
