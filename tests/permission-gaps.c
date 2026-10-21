/* SPDX-License-Identifier: GPL-2.0-only */
/* Issue 114 T5/T6/T8/T10. Dedicated VM only; no shared attribute validator.
 * plant LOWER before mounting; check LAYER LOWER q2|v1 afterwards.
 * Fixed bytes/expected membership come from spec-m2, not common/attr.c.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs_ioctl.h"

#define PATHCAP 4096
#define HIDDEN 70
#define MUST(c, why) do { if (!(c)) { fprintf(stderr, "PERMISSION_GAPS_FAIL: %s (errno=%d)\n", why, errno); exit(1); } } while (0)
static const char *layer, *lower;
static int legacy, phase;
static struct moob_query_result results[128];

static void path(char *out, const char *root, const char *rel)
{
	int n = snprintf(out, PATHCAP, "%s/%s", root, rel);

	MUST(n > 0 && n < PATHCAP, "fixture path length");
}

static void directory(const char *root, const char *rel, mode_t mode)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!mkdir(p, mode), "exclusive fixture directory");
}

static void attribute(const char *root, const char *rel, const char *name,
		      const unsigned char *value, size_t size)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!setxattr(p, name, value, size, 0), "fixture attribute");
}

static void file(const char *root, const char *rel)
{
	char p[PATHCAP];
	int fd;

	path(p, root, rel);
	fd = open(p, O_CREAT | O_EXCL | O_WRONLY, 0644);
	MUST(fd >= 0 && !close(fd), "exclusive fixture file");
}

static void mode(const char *root, const char *rel, mode_t bits)
{
	char p[PATHCAP];

	path(p, root, rel);
	MUST(!chmod(p, bits), "fixture chmod");
}

static int open_dir(const char *root, const char *rel)
{
	char p[PATHCAP];
	int fd;

	path(p, root, rel);
	fd = open(p, O_RDONLY | O_DIRECTORY);
	MUST(fd >= 0, "open directory as actual caller");
	return fd;
}

static void fill(struct moob_query2 *q, const char *name, unsigned int limit)
{
	memset(q, 0, sizeof(*q));
	q->version = MOOB_QUERY2_VERSION;
	q->nterms = 1;
	q->limit = limit;
	q->results = (uint64_t)(uintptr_t)results;
	MUST(strlen(name) < sizeof(q->terms[0].name), "query name length");
	strcpy(q->terms[0].name, name);
	q->terms[0].op = MOOB_OP_EXISTS;
}

/* shape 0: plain; 1: equality intersection; 2/3: ordered in both directions. */
static unsigned int query(int fd, const char *name, int shape, unsigned int limit,
			  unsigned int *truncated)
{
	struct moob_query2 q;
	unsigned int i;

	fill(&q, name, limit);
	if (legacy) {
		struct moob_query old = {0};

		old.version = MOOB_UAPI_VERSION;
		old.nterms = 1;
		old.limit = limit;
		old.results = q.results;
		old.terms[0] = q.terms[0];
		MUST(!ioctl(fd, MOOB_IOC_QUERY, &old), "v1 query");
		q.count = old.count;
		q.truncated = old.truncated;
	} else {
		if (shape == 1) {
			q.nterms = 2;
			q.terms[0].op = MOOB_OP_EQ;
			q.terms[0].type = 4;
			q.terms[0].len = 1;
			q.terms[0].value[0] = 1;
			q.terms[1] = q.terms[0];
			strcpy(q.terms[1].name, "tag");
		} else if (shape >= 2) {
			strcpy(q.order_by, "rank");
			q.order_type = 2;
			q.order = shape == 2 ? MOOB_ORDER_ASC : MOOB_ORDER_DESC;
		}
		MUST(!ioctl(fd, MOOB_IOC_QUERY2, &q), "QUERY2");
	}
	MUST(q.count <= limit && q.truncated <= 1, "reply bounds");
	for (i = 0; i < q.count; i++)
		MUST(!results[i].flags && memchr(results[i].path, 0, sizeof(results[i].path)),
		     "complete terminated result path");
	*truncated = q.truncated;
	return q.count;
}

static void child(void (*check)(void))
{
	pid_t pid = fork();
	int status;

	MUST(pid >= 0, "fork");
	if (!pid) {
		MUST(!setgroups(0, NULL) && !setresgid(1001, 1001, 1001) &&
		     !setresuid(1001, 1001, 1001), "drop credentials");
		MUST(geteuid() == 1001 && getegid() == 1001, "actual unprivileged identity");
		check();
		_exit(0);
	}
	MUST(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status),
	     "unprivileged child verdict");
}

static void target(void)
{
	int noexec;

	for (noexec = 0; noexec < 2; noexec++) {
		const char *d = noexec ? "noexec" : "control";
		struct moob_query2 q;
		struct moob_query2_batch batch = {0};
		char p[PATHCAP];
		int fd = open_dir(layer, d), lowfd = open_dir(lower, d), r;

		path(p, lower, d);
		MUST(!faccessat(AT_FDCWD, p, R_OK, AT_EACCESS), "T5 lower readable");
		r = faccessat(AT_FDCWD, p, X_OK, AT_EACCESS);
		MUST(noexec ? r == -1 && errno == EACCES : r == 0, "T5 lower search control");
		fill(&q, "test", 8);
		errno = 0;
		r = ioctl(fd, MOOB_IOC_QUERY2, &q);
		MUST(noexec ? r == -1 && errno == EACCES : !r && q.count == 1,
		     "T5 QUERY2 execute check decisive after successful open");
		fill(&q, "test", 8);
		batch.version = MOOB_QUERY2_VERSION;
		batch.nqueries = 1;
		batch.queries = (uint64_t)(uintptr_t)&q;
		errno = 0;
		r = ioctl(fd, MOOB_IOC_QUERY2_BATCH, &batch);
		MUST(noexec ? r == -1 && errno == EACCES : !r && q.count == 1,
		     "T5 QUERY2_BATCH execute check decisive");
		MUST(!close(fd) && !close(lowfd), "close T5 descriptors");
	}
}

static void count_channel(void)
{
	unsigned int truncated, count;
	int shape, fd = open_dir(layer, legacy ? "" : "count");
	char p[PATHCAP];
	unsigned char value[8];

	path(p, lower, "count/private/h00");
	if (phase == 1)
		MUST(getxattr(p, "user.moob.count", value, sizeof(value)) == 2, "T6 readable control");
	else
		MUST(getxattr(p, "user.moob.count", value, sizeof(value)) < 0 && errno == EACCES,
		     "T6 lower denies hidden candidate");
	for (shape = 0; shape < (legacy ? 1 : 4); shape++) {
		count = query(fd, "count", shape, 1, &truncated);
		MUST(count == (phase == 2 ? 0U : 1U), "T6 visible count");
		MUST(truncated == (phase == 1 ? 1U : 0U), "T6 hidden candidates do not set truncated");
	}
	MUST(!close(fd), "close T6 descriptor");
}

static void counts(void)
{
	unsigned int truncated;
	int fd;

	if (legacy) {
		mode(layer, "count/public", 0755);
		mode(layer, "count/private", 0755);
	}
	fd = open_dir(layer, legacy ? "" : "count");
	MUST(query(fd, "count", 0, 128, &truncated) == HIDDEN + 1 && !truncated,
	     "T6 warm index includes all future hidden candidates");
	MUST(!close(fd), "close warm descriptor");
	mode(layer, "count/private", 0700);
	for (phase = 0; phase < 3; phase++) {
		if (phase == 1)
			mode(layer, "count/private", 0755);
		if (phase == 2) {
			mode(layer, "count/private", 0700);
			mode(layer, "count/public", 0700);
		}
		child(count_channel);
	}
}

static const char *const dirs[] = { "public", "read-no-search", "no-read", "private", "branch/leaf" };
static void directory_answers(void)
{
	unsigned int count, truncated, i, j, expected = 0;
	int fd = open_dir(layer, "dirs");

	count = query(fd, "test", 0, 128, &truncated);
	MUST(!truncated, "T8 complete directory reply");
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		char p[PATHCAP], rel[128];
		unsigned char value[8];
		int readable, seen = 0;

		snprintf(rel, sizeof(rel), "dirs/%s", dirs[i]);
		path(p, lower, rel);
		readable = getxattr(p, "user.moob.test", value, sizeof(value)) == 2;
		MUST(readable || errno == EACCES, "T8 lower permission decision");
		for (j = 0; j < count; j++)
			seen += !strcmp(results[j].path, dirs[i]);
		MUST(seen == readable, "T8 attributed directory differs from lower getxattr");
		expected += readable;
	}
	MUST(count == expected, "T8 no extra or duplicate directories");
	MUST(!close(fd), "close T8 descriptor");
}

struct encoding { const char *rel; unsigned char bytes[10]; size_t size; int indexed; mode_t mode; uid_t uid; gid_t gid; };
static const struct encoding encodings[] = {
	{ "empty-string", {1}, 1, 1, 0644, 0, 0 },
	{ "unicode", {1, 0xc3, 0xa9}, 3, 1, 0644, 0, 0 },
	{ "true", {4, 1}, 2, 1, 0644, 0, 0 },
	{ "false", {4, 0}, 2, 1, 0644, 0, 0 },
	{ "integer", {2, 1, 0, 0, 0, 0, 0, 0, 0}, 9, 1, 0644, 0, 0 },
	{ "time", {5, 0, 0, 0, 0, 0, 0, 0, 0}, 9, 1, 0644, 0, 0 },
	{ "bad-bool", {4, 2}, 2, 0, 0644, 0, 0 },
	{ "bad-utf8", {1, 0xff}, 2, 0, 0644, 0, 0 },
	{ "nul-string", {1, 0}, 2, 0, 0644, 0, 0 },
	{ "short-int", {2, 0}, 2, 0, 0644, 0, 0 },
	{ "nan", {3, 0, 0, 0, 0, 0, 0, 0xf8, 0x7f}, 9, 0, 0644, 0, 0 },
	{ "raw", {6, 0}, 2, 0, 0644, 0, 0 },
	{ "no-type", {0}, 0, 0, 0644, 0, 0 },
	{ "unknown-type", {255}, 1, 0, 0644, 0, 0 },
	{ "private", {4, 1}, 2, 1, 0600, 0, 0 },
	{ "owner-denied", {4, 1}, 2, 1, 0004, 1001, 1001 },
	{ "group-denied", {4, 1}, 2, 1, 0404, 0, 1001 },
	{ "closed/valid", {4, 1}, 2, 1, 0644, 0, 0 },
};

static void encoding_answers(void)
{
	unsigned int i, j, count, truncated, expected = 0;
	int fd = open_dir(layer, legacy ? "" : "enc");

	count = query(fd, "probe", 0, 128, &truncated);
	MUST(!truncated, "T10 complete encoding reply");
	for (i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
		const struct encoding *e = &encodings[i];
		char p[PATHCAP], rel[128];
		unsigned char value[16];
		ssize_t size;
		int readable, wanted, seen = 0;

		snprintf(rel, sizeof(rel), "enc/%s", e->rel);
		path(p, lower, rel);
		size = getxattr(p, "user.moob.probe", value, sizeof(value));
		readable = size >= 0;
		MUST(readable || errno == EACCES, "T10 lower permission decision");
		if (readable)
			MUST((size_t)size == e->size && !memcmp(value, e->bytes, e->size), "T10 fixed bytes retained");
		wanted = e->indexed && readable;
		for (j = 0; j < count; j++)
			seen += !strcmp(results[j].path, legacy ? rel : e->rel);
		MUST(seen == wanted, "T10 independent encoding/permission membership");
		expected += wanted;
	}
	MUST(count == expected && expected == 6, "T10 known positive controls and no extras");
	MUST(!close(fd), "close T10 descriptor");
}

static void plant(void)
{
	static const unsigned char text[] = {1, 'v'}, boolean[] = {4, 1};
	unsigned int i;
	char rel[128], p[PATHCAP];

	umask(0);
	directory(lower, "control", 0755);
	directory(lower, "noexec", 0444);
	file(lower, "control/f"); file(lower, "noexec/f");
	attribute(lower, "control/f", "user.moob.test", text, sizeof(text));
	attribute(lower, "noexec/f", "user.moob.test", text, sizeof(text));
	directory(lower, "count", 0755);
	directory(lower, "count/public", 0755);
	directory(lower, "count/private", 0755);
	for (i = 0; i <= HIDDEN; i++) {
		unsigned char rank[] = {2, (unsigned char)i, 0, 0, 0, 0, 0, 0, 0};

		if (!i) strcpy(rel, "count/public/visible");
		else snprintf(rel, sizeof(rel), "count/private/h%02u", i - 1);
		file(lower, rel);
		attribute(lower, rel, "user.moob.count", boolean, sizeof(boolean));
		attribute(lower, rel, "user.moob.tag", boolean, sizeof(boolean));
		attribute(lower, rel, "user.moob.rank", rank, sizeof(rank));
	}
	directory(lower, "dirs", 0755);
	directory(lower, "dirs/branch", 0755);
	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
		static const mode_t bits[] = {0755, 0444, 0111, 0700, 0755};

		snprintf(rel, sizeof(rel), "dirs/%s", dirs[i]);
		directory(lower, rel, bits[i]);
		attribute(lower, rel, "user.moob.test", text, sizeof(text));
	}
	directory(lower, "enc", 0755);
	directory(lower, "enc/closed", 0700);
	for (i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
		const struct encoding *e = &encodings[i];

		snprintf(rel, sizeof(rel), "enc/%s", e->rel);
		file(lower, rel);
		attribute(lower, rel, "user.moob.probe", e->bytes, e->size);
		path(p, lower, rel);
		MUST(!chown(p, e->uid, e->gid), "T10 fixture ownership");
		mode(lower, rel, e->mode);
	}
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	if (argc == 3 && !strcmp(argv[1], "plant")) {
		lower = argv[2]; plant(); return 0;
	}
	MUST(argc == 5 && !strcmp(argv[1], "check"), "usage: permission-gaps plant LOWER | check LAYER LOWER q2|v1");
	layer = argv[2]; lower = argv[3]; legacy = !strcmp(argv[4], "v1");
	MUST(legacy || !strcmp(argv[4], "q2"), "known query mode");
	if (!legacy) {
		child(target);
		puts("TARGET_EXEC_PASS: uid=1001 opens readable domains; QUERY2 and BATCH enforce search");
	}
	counts();
	printf("TRUNCATED_PERMISSION_PASS: %s, uid=1001, 70 hidden candidates, 0/1/many visible controls\n", legacy ? "v1" : "QUERY2 plain/intersection/ordered asc/desc");
	if (!legacy) {
		child(directory_answers);
		mode(layer, "dirs/branch", 0700);
		child(directory_answers);
		puts("ATTRIBUTED_DIRECTORY_PASS: uid=1001, lower getxattr decisions before/after fallback");
	}
	child(encoding_answers);
	printf("INDEPENDENT_ENCODING_PASS: %s, 18 fixed fixtures, uid=1001, no shared validator\n", legacy ? "v1" : "QUERY2");
	return 0;
}
