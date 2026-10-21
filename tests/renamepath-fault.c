/* SPDX-License-Identifier: GPL-2.0-only */
/* R2-6 (journal 0170): fail the path allocation during rename, then evict the
 * dentries. An answer may say PATH_UNKNOWN, but must never name the old file.
 * Dedicated VM only: uses fail-nth, stack filters and drop_caches. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static void die(const char *s) { perror(s); exit(2); }

static void put(const char *path, const char *value)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(value);

	if (fd < 0 || write(fd, value, len) != (ssize_t)len)
		die(path);
	close(fd);
}

static void filter(unsigned long long start, unsigned long long end)
{
	const char *dirs[] = { "failslab", "fail_page_alloc" };
	char path[160], value[64];
	unsigned int i;

	for (i = 0; i < 2; i++) {
		snprintf(path, sizeof path, "/sys/kernel/debug/%s/require-start", dirs[i]);
		snprintf(value, sizeof value, "%llu", start);
		put(path, value);
		snprintf(path, sizeof path, "/sys/kernel/debug/%s/require-end", dirs[i]);
		snprintf(value, sizeof value, "%llu", end);
		put(path, value);
	}
}

static void query(int fd, struct moob_query_result *r)
{
	struct moob_query2 q = {0};

	memset(r, 0, sizeof *r);
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 1;
	q.results = (uint64_t)(uintptr_t)r;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	if (ioctl(fd, MOOB_IOC_QUERY2, &q) || q.count != 1)
		die("query must return the one attributed file");
}

int main(int argc, char **argv)
{
	char a[1024], b[1024], line[512], symbol[256], type, counter[32] = {0};
	unsigned long long addr, start = 0, end = ~0ULL;
	struct moob_query_result r;
	FILE *fp;
	int fd, dfd, nth, inject, hit = 0, rc;

	if (argc != 3) {
		fprintf(stderr, "usage: renamepath-fault <fresh domain> <0|1>\n");
		return 2;
	}
	setbuf(stdout, NULL);
	inject = atoi(argv[2]);
	if (mkdir(argv[1], 0755)) die("fresh domain");
	snprintf(a, sizeof a, "%s/a", argv[1]);
	snprintf(b, sizeof b, "%s/b", argv[1]);
	fd = open(a, O_CREAT | O_EXCL | O_WRONLY, 0644);
	if (fd < 0) die("create");
	close(fd);
	if (setxattr(a, "user.moob.kind", "\1note", 5, 0)) die("attribute");
	dfd = open(argv[1], O_RDONLY | O_DIRECTORY);
	if (dfd < 0) die("domain");
	query(dfd, &r);
	if (strcmp(r.path, "a")) die("initial name");
	if (inject) {
		fp = fopen("/proc/kallsyms", "r");
		if (!fp) die("kallsyms");
		while (fgets(line, sizeof line, fp))
			if (sscanf(line, "%llx %c %255s", &addr, &type, symbol) == 3 &&
			    !strcmp(symbol, "record_dentry_path")) start = addr;
		rewind(fp);
		while (fgets(line, sizeof line, fp))
			if (sscanf(line, "%llx %c %255s", &addr, &type, symbol) == 3 &&
			    addr > start && addr < end) end = addr;
		fclose(fp);
		if (!start || end == ~0ULL) die("path-recording symbol unavailable");
		filter(start, end);
	}
	nth = open("/proc/thread-self/fail-nth", O_RDWR);
	if (nth < 0) die("fail-nth");
	if (inject && pwrite(nth, "1", 1, 0) != 1) die("arm");
	rc = rename(a, b);
	if (pread(nth, counter, sizeof counter - 1, 0) < 0) die("counter");
	hit = inject && atoi(counter) == 0;
	if (pwrite(nth, "0", 1, 0) != 1) die("disarm");
	close(nth);
	filter(0, 0);
	if (rc || (inject && !hit)) die("rename or fault not reached");
	query(dfd, &r);
	if (strcmp(r.path, "b")) die("cached renamed path");
	put("/proc/sys/vm/drop_caches", "2");
	query(dfd, &r);
	printf("RENAMEPATH_RESULT injected=%d count=1 path=%s flags=%u\n",
	       hit, r.path, r.flags);
	close(dfd);
	if (!(r.flags & MOOB_RES_PATH_UNKNOWN) && strcmp(r.path, "b")) {
		fprintf(stderr, "R2-6: stale name after rename and dentry eviction\n");
		return 1;
	}
	return 0;
}
