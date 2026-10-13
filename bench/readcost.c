/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bench/readcost: what a client pays after a query, for the decision of docs/spec-m3.md §4
 * (READ_BATCH). Asks QUERY2 for N files of a domain and times, per round of N files:
 *   query      the QUERY2 call that returned them
 *   read       openat + read to the end + close of each, through the layer
 *   lower      the same files under the lower directory (--lower), without the layer
 *   pread      pread to the end on descriptors opened beforehand: the data copy alone
 *   attrs      flistxattr + fgetxattr of every user.moob.* attribute, on open descriptors
 *   attr1      one fgetxattr of the attribute NAME per file, on open descriptors
 *   lattrs     as attrs, on descriptors of the lower files (--lower)
 *   nullsys    3 * N system calls that do nothing, the entry cost a batch call would avoid
 * The most a batch read could save is read - pread: everything in `read` that is not the copy.
 *
 *   readcost --dir DOMAIN_DIR --attr NAME [--lower LOWER_DOMAIN_DIR] [--n 20] [--repeat 500]
 *
 * Files come from the term "NAME exists" in the index's scan order. Warm cache: the first round
 * is not counted. Prints the median per round and per file.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

enum { QUERY, READ, LOWER, PREAD, ATTRS, ATTR1, LATTRS, NULLSYS, NKINDS };
static const char *const kind_name[NKINDS] = { "query", "read", "lower", "pread", "attrs", "attr1",
						  "lattrs", "nullsys" };

static uint64_t now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void die(const char *what)
{
	fprintf(stderr, "readcost: %s: %s\n", what, strerror(errno));
	exit(1);
}

static int by_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;

	return x < y ? -1 : x > y;
}

/* Lists and reads every typed attribute of each open file; returns how many were read. */
static uint64_t read_attrs(const int *fds, __u32 n)
{
	static char names[1 << 16], value[MOOB_ATTR_VALUE_MAX];
	uint64_t count = 0;
	__u32 i;

	for (i = 0; i < n; i++) {
		ssize_t len = flistxattr(fds[i], names, sizeof names), off;

		if (len < 0) die("flistxattr");
		for (off = 0; off < len; off += (ssize_t)strlen(names + off) + 1) {
			if (strncmp(names + off, MOOB_ATTR_PREFIX, sizeof MOOB_ATTR_PREFIX - 1))
				continue;
			if (fgetxattr(fds[i], names + off, value, sizeof value) < 0) die("fgetxattr");
			count++;
		}
	}
	return count;
}

/* Opens, reads to the end and closes each path below dirfd; returns the bytes read. */
static uint64_t read_all(int dirfd, const struct moob_query_result *res, __u32 n, char *buf, size_t cap)
{
	uint64_t bytes = 0;
	__u32 i;

	for (i = 0; i < n; i++) {
		int fd = openat(dirfd, res[i].path, O_RDONLY | O_CLOEXEC);
		ssize_t r;

		if (fd < 0) die(res[i].path);
		while ((r = read(fd, buf, cap)) > 0)
			bytes += (uint64_t)r;
		if (r < 0) die("read");
		close(fd);
	}
	return bytes;
}

int main(int argc, char **argv)
{
	const char *dir = NULL, *lower = NULL, *attr = NULL;
	int a, repeat = 500, rep, k, dfd, lfd = -1, *fds;
	uint64_t *t[NKINDS], bytes = 0, nattrs = 0;
	struct moob_query_result *res;
	struct moob_query2 q;
	static char buf[1 << 16], value[MOOB_ATTR_VALUE_MAX], full[MOOB_ATTR_PREFIX_LEN + MOOB_QUERY_NAME_MAX + 1];
	__u32 n = 20, i;

	for (a = 1; a + 1 < argc; a += 2) {
		if (!strcmp(argv[a], "--dir")) dir = argv[a + 1];
		else if (!strcmp(argv[a], "--lower")) lower = argv[a + 1];
		else if (!strcmp(argv[a], "--attr")) attr = argv[a + 1];
		else if (!strcmp(argv[a], "--n")) n = (__u32)strtoul(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--repeat")) repeat = atoi(argv[a + 1]);
		else { fprintf(stderr, "readcost: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || !dir || !attr || strlen(attr) > MOOB_QUERY_NAME_MAX || n < 1 ||
	    n > MOOB_QUERY_MAX_LIMIT || repeat < 2) {
		fprintf(stderr, "usage: readcost --dir DOMAIN_DIR --attr NAME [--lower DIR] [--n N] [--repeat R]\n");
		return 64;
	}
	dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd < 0) die(dir);
	if (lower && (lfd = open(lower, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0) die(lower);
	res = calloc(n, sizeof *res);
	fds = calloc(n, sizeof *fds);
	for (k = 0; k < NKINDS; k++)
		t[k] = calloc((size_t)repeat, sizeof **t);
	if (!res || !fds || !t[NKINDS - 1]) { errno = ENOMEM; die("alloc"); }
	memset(&q, 0, sizeof q);
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = n;
	q.results = (__u64)(uintptr_t)res;
	memcpy(q.terms[0].name, attr, strlen(attr));
	snprintf(full, sizeof full, "%s%s", MOOB_ATTR_PREFIX, attr);
	q.terms[0].op = MOOB_OP_EXISTS;

	for (rep = 0; rep < repeat; rep++) {
		uint64_t t0 = now_ns();

		memset(q.cursor, 0, sizeof q.cursor);
		if (ioctl(dfd, MOOB_IOC_QUERY2, &q)) die("MOOB_IOC_QUERY2");
		t[QUERY][rep] = now_ns() - t0;
		if (q.count != n) { fprintf(stderr, "readcost: %u results, wanted %u\n", q.count, n); return 1; }

		t0 = now_ns();
		bytes = read_all(dfd, res, n, buf, sizeof buf);
		t[READ][rep] = now_ns() - t0;

		if (lfd >= 0) {
			t0 = now_ns();
			if (read_all(lfd, res, n, buf, sizeof buf) != bytes) { errno = EIO; die("lower differs"); }
			t[LOWER][rep] = now_ns() - t0;
		}

		for (i = 0; i < n; i++)
			if ((fds[i] = openat(dfd, res[i].path, O_RDONLY | O_CLOEXEC)) < 0) die(res[i].path);
		t0 = now_ns();
		for (i = 0; i < n; i++) {
			off_t off = 0;
			ssize_t r;

			while ((r = pread(fds[i], buf, sizeof buf, off)) > 0)
				off += r;
			if (r < 0) die("pread");
		}
		t[PREAD][rep] = now_ns() - t0;

		t0 = now_ns();
		nattrs = read_attrs(fds, n);
		t[ATTRS][rep] = now_ns() - t0;

		t0 = now_ns();
		for (i = 0; i < n; i++)
			if (fgetxattr(fds[i], full, value, sizeof value) < 0) die("fgetxattr");
		t[ATTR1][rep] = now_ns() - t0;
		for (i = 0; i < n; i++)
			close(fds[i]);

		if (lfd >= 0) {
			for (i = 0; i < n; i++)
				if ((fds[i] = openat(lfd, res[i].path, O_RDONLY | O_CLOEXEC)) < 0) die(res[i].path);
			t0 = now_ns();
			if (read_attrs(fds, n) != nattrs) { errno = EIO; die("lower attributes differ"); }
			t[LATTRS][rep] = now_ns() - t0;
			for (i = 0; i < n; i++)
				close(fds[i]);
		}

		t0 = now_ns();
		for (i = 0; i < 3 * n; i++)
			syscall(SYS_getppid);
		t[NULLSYS][rep] = now_ns() - t0;
	}

	printf("files=%u bytes_per_file=%llu attrs_per_file=%.1f rounds=%d\n", n,
	       (unsigned long long)(bytes / n), (double)nattrs / n, repeat - 1);
	puts("what\tround_p50_us\tper_file_us");
	for (k = 0; k < NKINDS; k++) {
		double us;

		if ((k == LOWER || k == LATTRS) && lfd < 0)
			continue;
		qsort(t[k] + 1, (size_t)repeat - 1, sizeof **t, by_u64);
		us = (double)t[k][1 + (repeat - 1) / 2] / 1e3;
		t[k][0] = t[k][1 + (repeat - 1) / 2];
		printf("%s\t%.1f\t%.2f\n", kind_name[k], us, us / n);
	}
	printf("ceiling of a batch read (read - pread)\t%.1f\t%.2f\n",
	       ((double)t[READ][0] - (double)t[PREAD][0]) / 1e3,
	       ((double)t[READ][0] - (double)t[PREAD][0]) / 1e3 / n);
	return 0;
}
