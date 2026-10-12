/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Query term parsing shared by moobfs-index (user-space oracle) and the
 * tests/query.c ioctl client. Syntax on the command line, repeated up to four
 * times: <name> <op> <type> <value>   e.g.  kind eq string decision
 * Ops: eq lt le gt ge prefix exists (exists takes "-" for type and value).
 */
#ifndef MOOBFS_QUERY_COMMON_H
#define MOOBFS_QUERY_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

static int parse_op(const char *s)
{
	static const struct { const char *n; int op; } ops[] = {
		{ "eq", MOOB_OP_EQ }, { "lt", MOOB_OP_LT }, { "le", MOOB_OP_LE }, { "gt", MOOB_OP_GT },
		{ "ge", MOOB_OP_GE }, { "prefix", MOOB_OP_PREFIX }, { "exists", MOOB_OP_EXISTS },
	};
	size_t i;

	for (i = 0; i < sizeof ops / sizeof ops[0]; i++)
		if (!strcmp(s, ops[i].n))
			return ops[i].op;
	return -1;
}

static int parse_type(const char *s)
{
	if (!strcmp(s, "string")) return MOOB_T_STRING;
	if (!strcmp(s, "int64")) return MOOB_T_INT64;
	if (!strcmp(s, "double")) return MOOB_T_DOUBLE;
	if (!strcmp(s, "bool")) return MOOB_T_BOOL;
	if (!strcmp(s, "time")) return MOOB_T_TIME;
	return -1;
}

/* Fills terms from argv[from..]; returns the number of terms or -1. */
static int parse_terms(int argc, char **argv, int from, struct moob_query_term *terms)
{
	int n = 0;

	while (from + 4 <= argc) {
		struct moob_query_term *t = &terms[n];
		char opbuf[16];
		size_t olen = strlen(argv[from + 1]);
		int missing = olen > 1 && olen < sizeof opbuf && argv[from + 1][olen - 1] == '?';
		int op;
		size_t nlen = strlen(argv[from]);

		/* "gt?" and the like: the term also matches files without the attribute */
		snprintf(opbuf, sizeof opbuf, "%.*s", (int)(missing ? olen - 1 : olen), argv[from + 1]);
		op = parse_op(opbuf);

		if (n == (int)MOOB_QUERY_MAX_TERMS || op < 0 || nlen > MOOB_QUERY_NAME_MAX)
			return -1;
		memset(t, 0, sizeof *t);
		memcpy(t->name, argv[from], nlen);
		t->op = (__u8)op;
		t->flags = missing ? MOOB_TERM_MISSING_OK : 0;
		if (op != MOOB_OP_EXISTS) {
			int type = parse_type(argv[from + 2]);
			const char *v = argv[from + 3];
			size_t vlen = strlen(v);

			if (type < 0)
				return -1;
			t->type = (__u8)type;
			if (type == MOOB_T_STRING) {
				if (vlen > MOOB_QUERY_VALUE_MAX) return -1;
				memcpy(t->value, v, vlen); t->len = (__u16)vlen;
			} else if (type == MOOB_T_INT64 || type == MOOB_T_TIME) {
				char *end; long long x;
				errno = 0; x = strtoll(v, &end, 10);
				if (errno || *end) return -1;
				moob_attr_put64(t->value, (ma_u64)x); t->len = 8;
			} else if (type == MOOB_T_DOUBLE) {
				char *end; double d; ma_u64 bits;
				errno = 0; d = strtod(v, &end);
				if (errno || *end) return -1;
				memcpy(&bits, &d, 8); moob_attr_put64(t->value, bits); t->len = 8;
			} else {
				if (!strcmp(v, "true") || !strcmp(v, "1")) t->value[0] = 1;
				else if (!strcmp(v, "false") || !strcmp(v, "0")) t->value[0] = 0;
				else return -1;
				t->len = 1;
			}
		}
		from += 4;
		n++;
	}
	return from == argc ? n : -1;
}

#endif
