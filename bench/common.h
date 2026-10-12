/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Shared pieces of the M4 benchmark harness (docs/benchmark-harness.md):
 * deterministic PRNG, manifest line format and the fixed-size latency record.
 * Pure helpers; the only I/O is bench_create_out(), which opens an output file.
 */
#ifndef MOOBFS_BENCH_COMMON_H
#define MOOBFS_BENCH_COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>

/* splitmix64: deterministic across platforms, good enough for workload shaping. */
static inline uint64_t bench_next(uint64_t *s)
{
	uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

/* Uniform double in [0,1). */
static inline double bench_unit(uint64_t *s)
{
	return (double)(bench_next(s) >> 11) * (1.0 / 9007199254740992.0);
}

/* Standard normal (Box-Muller, one sample per call). */
static inline double bench_normal(uint64_t *s)
{
	double u1 = bench_unit(s), u2 = bench_unit(s);

	if (u1 < 1e-300)
		u1 = 1e-300;
	return sqrt(-2.0 * log(u1)) * cos(6.283185307179586 * u2);
}

/* Fills buf with n pseudo-random bytes derived from seed; the same seed gives the same bytes. */
static inline void bench_fill(uint8_t *buf, size_t n, uint64_t seed)
{
	uint64_t s = seed ^ 0x5851f42d4c957f2dULL;
	size_t i = 0;

	while (i < n) {
		uint64_t v = bench_next(&s);
		size_t k = n - i < 8 ? n - i : 8;

		memcpy(buf + i, &v, k);
		i += k;
	}
}

/*
 * Manifest (text, one memory per line, tab-separated):
 *   <path>\t<size>\t<content_seed>\t<dup_of|->\t<name>=<type>:<value>[ <name>=<type>:<value>...]
 * Types: s string, i int64, d double, b bool, t time, r raw (value is the seed of 256 bytes).
 * Header lines start with '#'. Values never contain spaces or tabs.
 */
#define BENCH_MAX_ATTRS 20
#define BENCH_PATH_MAX 200

struct bench_attr {
	char name[65];
	char type;		/* s i d b t r */
	char value[80];
};

struct bench_line {
	char path[BENCH_PATH_MAX];
	uint64_t size;
	uint64_t seed;
	int64_t dup_of;		/* line index or -1 */
	int nattrs;
	struct bench_attr attrs[BENCH_MAX_ATTRS];
};

/* Parses one manifest line in place; returns 0 or -1. */
static inline int bench_parse_line(char *line, struct bench_line *out)
{
	char *save = NULL, *tok, *attrs;
	int field = 0;
	size_t n;

	memset(out, 0, sizeof *out);
	out->dup_of = -1;
	for (tok = line; *tok; tok++)
		if (*tok == '\n') { *tok = 0; break; }
	tok = strtok_r(line, "\t", &save);
	while (tok && field < 4) {
		switch (field) {
		case 0:
			n = strlen(tok);
			if (n >= sizeof out->path) return -1;
			memcpy(out->path, tok, n + 1); break;
		case 1: out->size = strtoull(tok, NULL, 10); break;
		case 2: out->seed = strtoull(tok, NULL, 10); break;
		case 3: out->dup_of = strcmp(tok, "-") ? strtoll(tok, NULL, 10) : -1; break;
		}
		field++;
		tok = strtok_r(NULL, "\t", &save);
	}
	if (field < 4)
		return -1;
	attrs = tok;
	save = NULL;
	for (tok = attrs ? strtok_r(attrs, " ", &save) : NULL; tok; tok = strtok_r(NULL, " ", &save)) {
		struct bench_attr *a;
		char *eq = strchr(tok, '='), *colon;

		if (!eq || out->nattrs == BENCH_MAX_ATTRS) return -1;
		a = &out->attrs[out->nattrs];
		*eq = 0;
		n = strlen(tok);
		if (n >= sizeof a->name || strlen(eq + 1) < 2 || eq[2] != ':') return -1;
		memcpy(a->name, tok, n + 1);
		a->type = eq[1];
		colon = eq + 3;
		n = strlen(colon);
		if (n >= sizeof a->value) return -1;
		memcpy(a->value, colon, n + 1);
		out->nattrs++;
	}
	return 0;
}

/*
 * Creates or truncates an output file (records, manifest) with mode 0644 before the umask,
 * instead of the 0666 that fopen(path, "w") would request. Returns NULL with errno set.
 */
static inline FILE *bench_create_out(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	FILE *f;

	if (fd < 0)
		return NULL;
	f = fdopen(fd, "wb");
	if (!f)
		close(fd);
	return f;
}

/* Latency record: 32 bytes, native endianness, one per call. */
struct bench_rec {
	uint32_t workload;	/* 1..9 */
	uint32_t op;		/* enum bench_op */
	uint64_t start_ns;	/* CLOCK_MONOTONIC */
	uint64_t dur_ns;
	uint32_t proc;
	int32_t result;		/* >= 0: bytes or result count; < 0: -errno */
};

/* For query records, op carries the shape: BENCH_OP_QUERY | (shape << 8), where shape is the
 * 1-based line of the query file (Q1..Q9 for bench/queries-w3.txt); 0 means unknown. */
#define BENCH_QUERY_OP(shape) ((uint32_t)BENCH_OP_QUERY | ((uint32_t)(shape) << 8))

enum bench_op {
	BENCH_OP_CREATE = 1,	/* W1: tmp + attrs + fsync + rename + fsync dir */
	BENCH_OP_READ = 2,	/* W2: open + read whole file */
	BENCH_OP_QUERY = 3,	/* W3: one QUERY ioctl */
	BENCH_OP_ATTR_SET = 4,	/* W4: setxattr of an existing attribute */
	BENCH_OP_ATTR_RM = 5,	/* W4: set + remove of a scratch attribute */
	BENCH_OP_MOUNT = 6,	/* W6/W7: mount (index rebuild) until the first QUERY answers */
	BENCH_OP_RENAME = 7,	/* W5: rename away and back */
	BENCH_OP_RECREATE = 8,	/* W5: unlink + create with the same attributes */
};

#endif
