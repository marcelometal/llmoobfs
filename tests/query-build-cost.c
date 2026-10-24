/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bounded, exploratory issue 118 probe. The guest script prepares the domain;
 * this process drops privileges, opens it, and measures only one QUERY2 ioctl.
 * The first, renamed and already-built cases use identical query arguments.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static int read_bytes(unsigned long long *out)
{
	char line[256];
	FILE *f = fopen("/proc/self/io", "r");
	int found = 0;

	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		if (sscanf(line, "read_bytes: %llu", out) == 1) {
			found = 1;
			break;
		}
	}
	if (ferror(f))
		found = 0;
	if (fclose(f) || !found)
		return -1;
	return 0;
}

/* The bounded ext4 probe mounts /dev/vda; device stats include kernel I/O. */
static int device_read_sectors(unsigned long long *out)
{
	unsigned long long reads, merged;
	FILE *f = fopen("/sys/block/vda/stat", "r");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%llu %llu %llu", &reads, &merged, out) == 3;
	if (fclose(f) || !ok)
		return -1;
	return 0;
}

static long long time_us(struct timeval t)
{
	return (long long)t.tv_sec * 1000000 + t.tv_usec;
}

static unsigned long long time_ns(struct timespec t)
{
	return (unsigned long long)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

int main(int argc, char **argv)
{
	struct moob_query_result result;
	struct moob_query2 q = {0};
	struct rusage before, after;
	struct timespec start, end;
	unsigned long long io_before, io_after;
	unsigned long long sectors_before, sectors_after;
	unsigned long uid;
	char *stop;
	int fd;

	if (argc != 4) {
		fprintf(stderr, "usage: query-build-cost DOMAIN UID PHASE\n");
		return 64;
	}
	errno = 0;
	uid = strtoul(argv[2], &stop, 10);
	if (errno || *stop || uid == 0 || uid > UINT_MAX) {
		fprintf(stderr, "query-build-cost: invalid unprivileged uid\n");
		return 64;
	}
	if (setgid((gid_t)uid) || setuid((uid_t)uid)) {
		perror("drop privileges");
		return 1;
	}
	/* setuid makes this process non-dumpable; permit reading its own /proc/self/io. */
	if (prctl(PR_SET_DUMPABLE, 1)) {
		perror("restore self accounting access");
		return 1;
	}
	fd = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		perror("open domain");
		return 1;
	}
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 1;
	q.results = (__u64)(uintptr_t)&result;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (read_bytes(&io_before) || device_read_sectors(&sectors_before) ||
	    getrusage(RUSAGE_SELF, &before) ||
	    clock_gettime(CLOCK_MONOTONIC, &start)) {
		perror("measurement before ioctl");
		return 1;
	}
	if (ioctl(fd, MOOB_IOC_QUERY2, &q)) {
		perror("QUERY2");
		return 1;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &end) ||
	    getrusage(RUSAGE_SELF, &after) || read_bytes(&io_after) ||
	    device_read_sectors(&sectors_after)) {
		perror("measurement after ioctl");
		return 1;
	}
	if (q.count != 1 || io_after < io_before || sectors_after < sectors_before) {
		fprintf(stderr, "query-build-cost: unexpected result or I/O counter\n");
		return 1;
	}
	printf("QUERY_BUILD_COST phase=%s wall_ns=%llu user_us=%lld sys_us=%lld read_bytes=%llu dev_read_sectors=%llu count=%u\n",
	       argv[3], time_ns(end) - time_ns(start),
	       time_us(after.ru_utime) - time_us(before.ru_utime),
	       time_us(after.ru_stime) - time_us(before.ru_stime),
	       io_after - io_before, sectors_after - sectors_before, q.count);
	close(fd);
	return 0;
}
