/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * asuid <uid> <program> [args...]: runs a program as uid and gid <uid> with no supplementary
 * groups, for VM tests of tools that, unlike tests/query.c, have no --uid option of their own
 * (the moobfs CLI, roadmap item C4).
 */
#define _GNU_SOURCE
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	uid_t u;

	if (argc < 3) {
		fprintf(stderr, "usage: asuid <uid> <program> [args...]\n");
		return 64;
	}
	u = (uid_t)strtoul(argv[1], NULL, 10);
	if (setgroups(0, NULL) || setgid(u) || setuid(u)) {
		perror("asuid");
		return 1;
	}
	execv(argv[2], argv + 2);
	perror(argv[2]);
	return 127;
}
