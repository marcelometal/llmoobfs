/* SPDX-License-Identifier: GPL-2.0-only */
/* One unprivileged QUERY2, asserting the explicit build-limit errno and no successful output. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

static int plant_empty(const char *dir, const char *count, int six_attrs)
{
	static const char time_value[9] = {5};
	static const struct {
		const char *name;
		const char *value;
		size_t len;
	} attrs[] = {
		{"user.moob.family", "\001note", 5},
		{"user.moob.tier", "\001working", 8},
		{"user.moob.kind", "\001fact", 5},
		{"user.moob.pinned", "\004\000", 2},
		{"user.moob.updated_at", time_value, sizeof(time_value)},
		{"user.moob.expires_at", time_value, sizeof(time_value)},
	};
	unsigned long n, i;
	size_t a;
	char *end;
	char path[PATH_MAX];
	int fd;

	errno = 0;
	n = strtoul(count, &end, 10);
	if (errno || *end || !n || n > 100000)
		return 64;
	for (i = 0; i < n; i++) {
		if (snprintf(path, sizeof(path), "%s/f%05lu", dir, i) >= (int)sizeof(path))
			return 1;
		fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0) {
			perror("plant empty file");
			return 1;
		}
		if (six_attrs)
			for (a = 0; a < sizeof(attrs) / sizeof(attrs[0]); a++)
				if (fsetxattr(fd, attrs[a].name, attrs[a].value,
					     attrs[a].len, 0)) {
					perror("plant typed attribute");
					close(fd);
					return 1;
				}
		if (close(fd)) {
			perror("close planted file");
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct moob_query_result result, before;
	struct moob_query2 q = {0};
	unsigned long uid;
	char *end;
	int fd, want, got;

	if (argc != 4) {
		fprintf(stderr, "usage: query-build-limit plant|plant6 DIR COUNT | DOMAIN UID ok|quota|work|timeout\n");
		return 64;
	}
	if (!strcmp(argv[1], "plant"))
		return plant_empty(argv[2], argv[3], 0);
	if (!strcmp(argv[1], "plant6"))
		return plant_empty(argv[2], argv[3], 1);
	errno = 0;
	uid = strtoul(argv[2], &end, 10);
	if (errno || *end || !uid || uid > UINT_MAX) {
		fprintf(stderr, "query-build-limit: invalid uid\n");
		return 64;
	}
	if (!strcmp(argv[3], "ok"))
		want = 0;
	else if (!strcmp(argv[3], "quota"))
		want = EDQUOT;
	else if (!strcmp(argv[3], "work"))
		want = E2BIG;
	else if (!strcmp(argv[3], "timeout"))
		want = ETIMEDOUT;
	else
		return 64;
	if (setgid((gid_t)uid) || setuid((uid_t)uid)) {
		perror("drop privileges");
		return 1;
	}
	fd = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		perror("open domain");
		return 1;
	}
	memset(&result, 0xa5, sizeof(result));
	before = result;
	q.version = MOOB_QUERY2_VERSION;
	q.nterms = 1;
	q.limit = 1;
	q.results = (__u64)(uintptr_t)&result;
	q.count = 777;
	q.truncated = 777;
	strcpy(q.terms[0].name, "kind");
	q.terms[0].op = MOOB_OP_EXISTS;
	errno = 0;
	got = ioctl(fd, MOOB_IOC_QUERY2, &q) ? errno : 0;
	close(fd);
	if (got != want || (want == 0 && q.count != 1) ||
	    (want && (q.count != 777 || q.truncated != 777 ||
		     memcmp(&result, &before, sizeof(result)) ||
		     memcmp(q.cursor, (unsigned char[MOOB_QUERY2_CURSOR]){0},
			    sizeof(q.cursor))))) {
		fprintf(stderr, "query-build-limit: got errno=%d, expected=%d, count=%u\n",
			got, want, q.count);
		return 1;
	}
	printf("QUERY_BUILD_LIMIT_PASS expected_errno=%d\n", want);
	return 0;
}
