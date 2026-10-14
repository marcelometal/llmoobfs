/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * buildrace: the first query on a domain builds its index while the domain is being written
 * (ADR-0012, journal 0098). For each domain directory r000, r001, ... of a layer mount, three
 * threads set, replace and remove attributes, rename files into and out of a subdirectory,
 * unlink and re-create files and rename directories, all inside that domain, while the main
 * thread issues the first QUERY2 on it. The writers then stop. The caller (vm/suite.sh)
 * compares every domain with the oracle afterwards: whatever interleaving happened, the index
 * must equal what the lower filesystem holds.
 *
 *   buildrace <layer mount> <domains> <files per domain>
 *
 * Each domain must already hold f0000 ... and the directories s, s/t and u. EAGAIN from the query
 * (the domain kept changing under the build) is retried and counted. Prints BUILDRACE_DONE with
 * the totals; exits 1 on any other query error.
 */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <linux/types.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

static char dom[600];
static int nfiles;
static atomic_int stop;
static atomic_ulong ops;

static unsigned int rnd(unsigned int *s)
{
	*s = *s * 1103515245u + 12345u;
	return *s >> 8;
}

static void set_kind(const char *path, unsigned int k)
{
	char v[3] = { MOOB_T_STRING, 'k', (char)('0' + k % 4) };

	setxattr(path, "user.moob.kind", v, sizeof v, 0);
}

static void set_score(const char *path, unsigned int n)
{
	char v[9] = { MOOB_T_INT64 };
	uint64_t bits = n % 100;
	int i;

	for (i = 0; i < 8; i++) v[1 + i] = (char)(bits >> (8 * i));
	setxattr(path, "user.moob.score", v, sizeof v, 0);
}

static void *writer(void *arg)
{
	unsigned int seed = (unsigned int)(uintptr_t)arg * 2654435761u + 1;
	char a[700], b[700];

	while (!atomic_load(&stop)) {
		unsigned int f = rnd(&seed) % (unsigned int)nfiles, op = rnd(&seed) % 100;
		int fd;

		snprintf(a, sizeof a, "%s/f%04u", dom, f);
		snprintf(b, sizeof b, "%s/s/f%04u", dom, f);
		if (op < 30) { set_kind(a, rnd(&seed)); set_kind(b, rnd(&seed)); }
		else if (op < 45) { set_score(a, rnd(&seed)); set_score(b, rnd(&seed)); }
		else if (op < 60) { removexattr(a, "user.moob.kind"); removexattr(b, "user.moob.kind"); }
		else if (op < 75) rename(a, b);
		else if (op < 90) rename(b, a);
		else if (op < 97) {
			unlink(a); unlink(b);
			fd = open(a, O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (fd >= 0) { close(fd); set_kind(a, rnd(&seed)); set_score(a, rnd(&seed)); }
		} else {	/* a directory rename: the build has to start again */
			snprintf(a, sizeof a, "%s/s/t", dom);	/* same depth: a deeper target is EXDEV */
			snprintf(b, sizeof b, "%s/u/t", dom);
			if (rename(a, b)) rename(b, a);
		}
		atomic_fetch_add(&ops, 1);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	static struct moob_query_result res[64];
	unsigned long eagain = 0, queries = 0;
	int ndom, d, t;

	if (argc != 4) { fprintf(stderr, "usage: buildrace <layer mount> <domains> <files per domain>\n"); return 64; }
	ndom = atoi(argv[2]); nfiles = atoi(argv[3]);
	if (ndom < 1 || nfiles < 1) return 64;
	for (d = 0; d < ndom; d++) {
		struct timespec ts = { 0, (long)(d % 5) * 1000000L };
		struct moob_query2 q;
		pthread_t th[3];
		int dfd, r;

		snprintf(dom, sizeof dom, "%s/r%03d", argv[1], d);
		dfd = open(dom, O_RDONLY | O_DIRECTORY);
		if (dfd < 0) { perror(dom); return 2; }
		atomic_store(&stop, 0);
		for (t = 0; t < 3; t++)
			if (pthread_create(&th[t], NULL, writer, (void *)(uintptr_t)(d * 3 + t + 1))) { perror("pthread_create"); return 2; }
		nanosleep(&ts, NULL);	/* 0 to 4 ms: the build starts at different points of the writing */
		do {
			memset(&q, 0, sizeof q);
			q.version = MOOB_QUERY2_VERSION; q.nterms = 1; q.limit = 64;
			q.results = (__u64)(uintptr_t)res;
			snprintf(q.terms[0].name, sizeof q.terms[0].name, "kind");
			q.terms[0].op = MOOB_OP_EXISTS;
			r = ioctl(dfd, MOOB_IOC_QUERY2, &q);
			queries++;
			if (r && errno == EAGAIN) eagain++;
		} while (r && errno == EAGAIN && eagain < 200);
		if (r) { fprintf(stderr, "buildrace: QUERY2 on %s: %s\n", dom, strerror(errno)); atomic_store(&stop, 1); return 1; }
		ts.tv_nsec = 3000000L;
		nanosleep(&ts, NULL);	/* writes on the freshly built domain too */
		atomic_store(&stop, 1);
		for (t = 0; t < 3; t++) pthread_join(th[t], NULL);
		close(dfd);
	}
	printf("BUILDRACE_DONE: domains=%d files=%d writes=%lu queries=%lu eagain=%lu\n", ndom, nfiles,
	       atomic_load(&ops), queries, eagain);
	return 0;
}
