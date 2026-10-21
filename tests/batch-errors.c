/* SPDX-License-Identifier: GPL-2.0-only */
/* Journal 0173: observe QUERY2_BATCH's non-atomic output on error (issue 112).
 * Run in the suite's static domain containing readable files with a kind attribute.
 * This is a dedicated-VM helper; no allocator fault injection is required.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

#define CHECK(c) do { if (!(c)) { \
	fprintf(stderr, "BATCH_ERROR_FAIL line %d: %s (errno=%d)\n", __LINE__, #c, errno); \
	return 1; } } while (0)

static void fill(struct moob_query2 *q, struct moob_query_result *r)
{
	memset(q, 0, sizeof(*q));
	q->version = MOOB_QUERY2_VERSION;
	q->nterms = 1;
	q->limit = 1;
	q->count = 777;
	q->results = (uint64_t)(uintptr_t)r;
	strcpy(q->terms[0].name, "kind");
	q->terms[0].op = MOOB_OP_EXISTS;
}

int main(int argc, char **argv)
{
	struct moob_query2 ref, qs[3], before[3];
	struct moob_query2_batch b = {0};
	struct moob_query_result expected, out[3], untouched[3];
	struct moob_query2 *readonly;
	void *bad;
	long page;
	size_t bytes;
	int fd;
	unsigned int i;

	CHECK(argc == 2);
	CHECK(!setgroups(0, NULL) && !setresgid(1001, 1001, 1001) &&
	      !setresuid(1001, 1001, 1001));
	fd = open(argv[1], O_RDONLY | O_DIRECTORY);
	CHECK(fd >= 0);
	fill(&ref, &expected);
	CHECK(!ioctl(fd, MOOB_IOC_QUERY2, &ref));
	CHECK(ref.count == 1 && expected.ino && expected.path_len &&
	      expected.path_len < sizeof(expected.path));
	page = sysconf(_SC_PAGESIZE);
	CHECK(page > 0);
	bad = mmap(NULL, (size_t)page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(bad != MAP_FAILED);
	b.version = MOOB_QUERY2_VERSION;
	b.nqueries = 3;
	b.queries = (uint64_t)(uintptr_t)qs;
	memset(untouched, 0xa5, sizeof(untouched));

	/* An invalid second request is refused before any result is copied. */
	memcpy(out, untouched, sizeof(out));
	for (i = 0; i < 3; i++)
		fill(&qs[i], &out[i]);
	qs[1].limit = 0;
	memcpy(before, qs, sizeof(qs));
	errno = 0;
	CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EINVAL);
	CHECK(!memcmp(qs, before, sizeof(qs)) && !memcmp(out, untouched, sizeof(out)));
	puts("BATCH_VALIDATION_ATOMIC_PASS");

	/* Valid requests, but copying the second result faults after the first copy. */
	for (i = 0; i < 3; i++)
		fill(&qs[i], &out[i]);
	qs[1].results = (uint64_t)(uintptr_t)bad;
	memcpy(before, qs, sizeof(qs));
	errno = 0;
	CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EFAULT);
	CHECK(!memcmp(&out[0], &expected, sizeof(expected)));
	CHECK(!memcmp(&out[1], &untouched[1], 2 * sizeof(out[0])));
	CHECK(!memcmp(qs, before, sizeof(qs)));
	puts("BATCH_LATE_RESULT_FAULT_PASS");

	/* All results may be copied before writing the request headers fails. */
	bytes = (sizeof(qs) + (size_t)page - 1) / (size_t)page * (size_t)page;
	readonly = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK(readonly != MAP_FAILED);
	memcpy(out, untouched, sizeof(out));
	for (i = 0; i < 3; i++)
		fill(&readonly[i], &out[i]);
	memcpy(before, readonly, sizeof(before));
	CHECK(!mprotect(readonly, bytes, PROT_READ));
	b.queries = (uint64_t)(uintptr_t)readonly;
	errno = 0;
	CHECK(ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b) == -1 && errno == EFAULT);
	for (i = 0; i < 3; i++)
		CHECK(!memcmp(&out[i], &expected, sizeof(expected)));
	CHECK(!memcmp(readonly, before, sizeof(before)));
	CHECK(!munmap(readonly, bytes));
	puts("BATCH_HEADER_FAULT_PASS");

	/* Reinitialized outputs and valid buffers succeed after the failures. */
	memcpy(out, untouched, sizeof(out));
	for (i = 0; i < 3; i++)
		fill(&qs[i], &out[i]);
	b.queries = (uint64_t)(uintptr_t)qs;
	CHECK(!ioctl(fd, MOOB_IOC_QUERY2_BATCH, &b));
	for (i = 0; i < 3; i++) {
		CHECK(qs[i].count == ref.count && qs[i].truncated == ref.truncated);
		CHECK(!memcmp(qs[i].cursor, ref.cursor, sizeof(ref.cursor)));
		CHECK(!memcmp(&out[i], &expected, sizeof(expected)));
	}
	CHECK(!munmap(bad, (size_t)page));
	CHECK(!close(fd));
	puts("BATCH_RUNTIME_ERRORS_PASS");
	return 0;
}
