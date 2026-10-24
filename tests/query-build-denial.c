/* SPDX-License-Identifier: GPL-2.0-only */
/* Measure one cold QUERY2 over an empty-attribute domain, with or without E2BIG. */
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
	while (fgets(line, sizeof(line), f)) {
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

static int device_sectors(unsigned long long *out)
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
	return (unsigned long long)t.tv_sec * 1000000000ULL +
	       (unsigned long long)t.tv_nsec;
}

int main(int argc, char **argv)
{
	struct moob_query_result result, original;
	struct moob_query2 q = {0};
	struct rusage cpu_before, cpu_after;
	struct timespec start, end;
	unsigned long long io_before, io_after, dev_before, dev_after;
	unsigned long uid;
	char *stop;
	int fd, error;

	if (argc != 3) {
		fprintf(stderr, "usage: query-build-denial DOMAIN UID\n");
		return 64;
	}
	errno = 0;
	uid = strtoul(argv[2], &stop, 10);
	if (errno || *stop || !uid || uid > UINT_MAX)
		return 64;
	if (setgid((gid_t)uid) || setuid((uid_t)uid) ||
	    prctl(PR_SET_DUMPABLE, 1)) {
		perror("drop privileges or restore self accounting");
		return 1;
	}
	fd = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		perror("open domain");
		return 1;
	}
	memset(&result, 0xa5, sizeof(result));
	original = result;
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 1;
	q.results = (__u64)(uintptr_t)&result;
	q.count = 777;
	q.truncated = 777;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (read_bytes(&io_before) || device_sectors(&dev_before) ||
	    getrusage(RUSAGE_SELF, &cpu_before) ||
	    clock_gettime(CLOCK_MONOTONIC, &start)) {
		perror("measurement before ioctl");
		return 1;
	}
	errno = 0;
	error = ioctl(fd, MOOB_IOC_QUERY2, &q) ? errno : 0;
	if (clock_gettime(CLOCK_MONOTONIC, &end) ||
	    getrusage(RUSAGE_SELF, &cpu_after) ||
	    read_bytes(&io_after) || device_sectors(&dev_after)) {
		perror("measurement after ioctl");
		return 1;
	}
	if ((error == 0 && (q.count != 0 || q.truncated)) ||
	    (error == E2BIG && (q.count != 777 || q.truncated != 777 ||
			     memcmp(&result, &original, sizeof(result)) ||
			     memcmp(q.cursor,
				    (unsigned char[MOOB_QUERY2_CURSOR]){0},
				    sizeof(q.cursor)))) ||
	    (error != 0 && error != E2BIG) || io_after < io_before ||
	    dev_after < dev_before) {
		fprintf(stderr, "query-build-denial: unexpected errno=%d count=%u truncated=%u\n",
			error, q.count, q.truncated);
		return 1;
	}
	printf("QUERY_BUILD_DENIAL outcome=%s wall_ns=%llu user_us=%lld sys_us=%lld read_bytes=%llu dev_read_sectors=%llu count=%u\n",
	       error == E2BIG ? "denied" : "full",
	       time_ns(end) - time_ns(start),
	       time_us(cpu_after.ru_utime) - time_us(cpu_before.ru_utime),
	       time_us(cpu_after.ru_stime) - time_us(cpu_before.ru_stime),
	       io_after - io_before, dev_after - dev_before, q.count);
	close(fd);
	return 0;
}
