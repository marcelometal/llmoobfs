/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * reconf: a change of a layer mount option through the new mount API (fspick, fsconfig) is
 * refused with EINVAL, and the mount keeps working (journal 0106).
 *
 *   reconf <layer mount>
 *
 * Prints RECONF_REFUSED and exits 0 when fsconfig(FSCONFIG_SET_STRING, "index_budget") fails
 * with EINVAL; exits 1 when it is accepted, 2 when the API is unavailable.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int fd, rc;

	if (argc != 2)
		return 2;
	fd = (int)syscall(SYS_fspick, AT_FDCWD, argv[1], FSPICK_CLOEXEC);
	if (fd < 0) {
		perror("fspick");
		return 2;
	}
	rc = (int)syscall(SYS_fsconfig, fd, FSCONFIG_SET_STRING, "index_budget", "64M", 0);
	if (rc == 0) {
		fprintf(stderr, "reconf: index_budget accepted on reconfigure\n");
		close(fd);
		return 1;
	}
	if (errno != EINVAL) {
		perror("fsconfig");
		close(fd);
		return 1;
	}
	close(fd);
	printf("RECONF_REFUSED\n");
	return 0;
}
