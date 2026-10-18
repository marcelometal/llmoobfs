/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * reconf: layer mount options through the new mount API on reconfigure (fspick, fsconfig).
 * A change is refused with EINVAL (journal 0106); an option repeated with its current value is
 * accepted, as libmount sends every option of the mount again on "mount -o remount"
 * (journal 0118), and the reconfiguration then completes.
 *
 *   reconf <layer mount> <lower directory> <domain_depth of the mount>
 *
 * The layer must be mounted without nolinks and with the default index budget (128M).
 * Prints RECONF_PASS and exits 0 when every case behaves; exits 1 otherwise, 2 when the API
 * is unavailable.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/mount.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static const char *layer;

/* fsconfig of one option on a fresh fspick context; returns 0 or the errno. */
static int set_one(unsigned int cmd, const char *key, const char *value, int reconfigure)
{
	int fd, rc, err = 0;

	fd = (int)syscall(SYS_fspick, AT_FDCWD, layer, FSPICK_CLOEXEC);
	if (fd < 0) {
		perror("fspick");
		return -1;
	}
	rc = (int)syscall(SYS_fsconfig, fd, cmd, key, value, 0);
	if (rc)
		err = errno;
	else if (reconfigure &&
		 syscall(SYS_fsconfig, fd, FSCONFIG_CMD_RECONFIGURE, NULL, NULL, 0))
		err = errno;
	close(fd);
	return err;
}

static int expect(const char *what, int got, int want)
{
	if (got == want)
		return 0;
	fprintf(stderr, "reconf: %s: %s, expected %s\n", what, got ? strerror(got) : "accepted",
		want ? strerror(want) : "accepted");
	return 1;
}

int main(int argc, char **argv)
{
	int bad = 0;

	if (argc != 4)
		return 2;
	layer = argv[1];
	if (set_one(FSCONFIG_SET_STRING, "index_budget", "64M", 0) < 0)
		return 2;
	/* changes: refused */
	bad |= expect("index_budget 64M", set_one(FSCONFIG_SET_STRING, "index_budget", "64M", 0),
		      EINVAL);
	bad |= expect("nolinks", set_one(FSCONFIG_SET_FLAG, "nolinks", NULL, 0), EINVAL);
	bad |= expect("lower=/", set_one(FSCONFIG_SET_STRING, "lower", "/", 0), EINVAL);
	bad |= expect("domain_depth 7", set_one(FSCONFIG_SET_STRING, "domain_depth", "7", 0),
		      EINVAL);
	/* the current values, as libmount repeats them: accepted, and the remount completes */
	bad |= expect("lower (current)", set_one(FSCONFIG_SET_STRING, "lower", argv[2], 1), 0);
	bad |= expect("domain_depth (current)",
		      set_one(FSCONFIG_SET_STRING, "domain_depth", argv[3], 1), 0);
	bad |= expect("index_budget (current)",
		      set_one(FSCONFIG_SET_STRING, "index_budget", "128M", 1), 0);
	if (bad)
		return 1;
	printf("RECONF_PASS\n");
	return 0;
}
