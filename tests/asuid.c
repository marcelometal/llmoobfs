/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * asuid <uid> <program> [args...]: runs a program as uid and gid <uid> with no supplementary
 * groups, for VM tests of tools that, unlike tests/query.c, have no --uid option of their own
 * (the moobfs CLI, roadmap item C4). The program is opened before the switch and run from that
 * descriptor, so the user need not be able to reach the directory that holds it (the KASAN
 * harness keeps its binaries under the runner's home, journal 0198).
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char **argv)
{
	uid_t u;
	int fd;

	if (argc < 3) {
		fprintf(stderr, "usage: asuid <uid> <program> [args...]\n");
		return 64;
	}
	u = (uid_t)strtoul(argv[1], NULL, 10);
	fd = open(argv[2], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror(argv[2]);
		return 127;
	}
	if (setgroups(0, NULL) || setgid(u) || setuid(u)) {
		perror("asuid");
		return 1;
	}
	fexecve(fd, argv + 2, environ);
	perror(argv[2]);
	return 127;
}
