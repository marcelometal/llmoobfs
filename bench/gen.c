/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * bench/gen: deterministic dataset manifest for the M4 benchmark
 * (docs/benchmark-harness.md §3). The same arguments always produce the same
 * bytes, on any platform. No files other than the manifest are written.
 *
 *   gen --seed N --inodes N --profile 6|8|16 [--namespaces 8] [--agents 32] [--out FILE]
 *
 * Profiles: 6 = the fixed set (namespace, agent, memory_id, kind, score, created);
 * 8 adds pinned and weight; 16 adds tag1..tag7 and raw (raw is never indexed).
 * The specification listed "3" for the smallest profile, but it also fixes six
 * attributes for every profile; 6 is the consistent reading and is recorded as such.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "common.h"

static const char *kinds[6] = { "note", "decision", "fact", "task", "summary", "log" };

/* Zipf-like choice over n values with exponent 1: index 0 most frequent. */
static unsigned zipf(uint64_t *s, unsigned n, const double *cdf)
{
	double u = bench_unit(s);
	unsigned i;

	for (i = 0; i + 1 < n; i++)
		if (u < cdf[i])
			break;
	return i;
}

static void zipf_cdf(double *cdf, unsigned n)
{
	double h = 0;
	unsigned i;

	for (i = 1; i <= n; i++) h += 1.0 / (double)i;
	cdf[0] = (1.0 / 1.0) / h;
	for (i = 1; i < n; i++) cdf[i] = cdf[i - 1] + (1.0 / (double)(i + 1)) / h;
}

/* Textual UUID-like id: time-ordered prefix plus random tail, 36 chars. */
static void make_id(char *out, uint64_t created_ns, uint64_t *s)
{
	uint64_t ms = created_ns / 1000000ULL, r1 = bench_next(s), r2 = bench_next(s);

	snprintf(out, 37, "%08" PRIx64 "-%04" PRIx64 "-7%03" PRIx64 "-%04" PRIx64 "-%012" PRIx64,
		 (uint64_t)((ms >> 16) & 0xffffffffULL), (uint64_t)(ms & 0xffffULL), (uint64_t)(r1 & 0xfffULL),
		 (uint64_t)(0x8000ULL | (r2 & 0x3fffULL)), (uint64_t)((r1 >> 12) & 0xffffffffffffULL));
}

int main(int argc, char **argv)
{
	uint64_t seed = 1, s;
	unsigned long inodes = 1000, i;
	unsigned profile = 6, nns = 8, nagents = 32;
	const char *out = NULL;
	FILE *f = stdout;
	double kcdf[6], tcdf[50];
	uint64_t created = 1760000000000000000ULL;	/* 2025-10-09T08:53:20Z, nanoseconds */
	unsigned long kind_count[6] = { 0 }, dups = 0;
	uint64_t *sizes;
	int a;

	for (a = 1; a + 1 < argc; a += 2) {
		if (!strcmp(argv[a], "--seed")) seed = strtoull(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--inodes")) inodes = strtoul(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--profile")) profile = (unsigned)strtoul(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--namespaces")) nns = (unsigned)strtoul(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--agents")) nagents = (unsigned)strtoul(argv[a + 1], NULL, 10);
		else if (!strcmp(argv[a], "--out")) out = argv[a + 1];
		else { fprintf(stderr, "gen: unknown option %s\n", argv[a]); return 64; }
	}
	if (a != argc || (profile != 6 && profile != 8 && profile != 16) || !inodes || !nns || !nagents) {
		fprintf(stderr, "usage: gen --seed N --inodes N --profile 6|8|16 [--namespaces N] [--agents N] [--out FILE]\n");
		return 64;
	}
	if (out && !(f = bench_create_out(out))) { perror(out); return 1; }
	sizes = calloc(inodes, sizeof *sizes);
	if (!sizes) { perror("calloc"); return 1; }
	s = seed;
	zipf_cdf(kcdf, 6);
	zipf_cdf(tcdf, 50);
	fprintf(f, "# moobfs-bench manifest v1 seed=%" PRIu64 " inodes=%lu profile=%u namespaces=%u agents=%u\n",
		seed, inodes, profile, nns, nagents);
	fprintf(f, "# columns: path size content_seed dup_of attrs(name=type:value ...)\n");
	fprintf(f, "# sizes: log-normal, median 2048 bytes, clamped to [64, 65536]; 15%% byte-duplicates of an earlier memory\n");
	fprintf(f, "# kind: Zipf over 6 values; score: uniform int64 0..1000; created: increasing time with jitter\n");
	if (profile >= 8)
		fprintf(f, "# pinned: bool, 10%% true; weight: double, normal(0,1), present on 30%% of the memories\n");
	if (profile == 16)
		fprintf(f, "# tag1..tag7: Zipf over 50 values; raw: 256 opaque bytes from the given seed (not indexed)\n");
	for (i = 0; i < inodes; i++) {
		unsigned ns = (unsigned)(bench_next(&s) % nns), ag = (unsigned)(bench_next(&s) % nagents);
		unsigned k = zipf(&s, 6, kcdf);
		uint64_t score = bench_next(&s) % 1001ULL;
		double ln = exp(log(2048.0) + 0.9 * bench_normal(&s));
		uint64_t size = (uint64_t)ln, cseed;
		int64_t dup = -1;
		char id[37];

		if (size < 64) size = 64;
		if (size > 65536) size = 65536;
		created += 1000000ULL * (1 + bench_next(&s) % 2000ULL);	/* 1 ms .. 2 s apart */
		make_id(id, created, &s);
		if (i > 0 && bench_unit(&s) < 0.15) {
			dup = (int64_t)(bench_next(&s) % i);
			size = sizes[dup];	/* a duplicate has the same bytes, so the same size */
			dups++;
		}
		sizes[i] = size;
		cseed = dup >= 0 ? 0 : bench_next(&s);
		kind_count[k]++;
		fprintf(f, "ns%u/a%02u/m%lu.md\t%" PRIu64 "\t%" PRIu64 "\t", ns, ag, i, size, cseed);
		if (dup >= 0) fprintf(f, "%" PRId64, dup); else fputc('-', f);
		fprintf(f, "\tnamespace=s:ns%u agent=s:a%02u memory_id=s:%s kind=s:%s score=i:%" PRIu64 " created=t:%" PRIu64,
			ns, ag, id, kinds[k], score, created);
		if (profile >= 8) {
			int pinned = bench_unit(&s) < 0.10;
			double w = bench_normal(&s);

			/* weight exists on ~30% of memories so that "weight exists" (Q9) is selective */
			if (bench_unit(&s) < 0.30)
				fprintf(f, " pinned=b:%d weight=d:%.6f", pinned, w);
			else
				fprintf(f, " pinned=b:%d", pinned);
		}
		if (profile == 16) {
			unsigned t;

			for (t = 1; t <= 7; t++)
				fprintf(f, " tag%u=s:v%02u", t, zipf(&s, 50, tcdf));
			fprintf(f, " raw=r:%" PRIu64, bench_next(&s));
		}
		fputc('\n', f);
	}
	fprintf(f, "# summary: duplicates=%lu kinds=%lu/%lu/%lu/%lu/%lu/%lu\n", dups,
		kind_count[0], kind_count[1], kind_count[2], kind_count[3], kind_count[4], kind_count[5]);
	free(sizes);
	if (f != stdout && fclose(f)) { perror(out); return 1; }
	return 0;
}
