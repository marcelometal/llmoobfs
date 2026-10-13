/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs-check: validate every typed attribute under a lower directory (or a
 * mounted layer) and the format marker on its root. Diagnoses only; never
 * repairs. Output is one JSON object. Exit: 0 clean, 2 problems found, 1 error.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ftw.h>
#include <sys/xattr.h>
#include "moobfs_attr.h"

static unsigned long files, attrs, problems;
/* identity records for the (namespace, memory_id) uniqueness check */
struct ident { char ns[1025]; char id[1025]; dev_t dev; ino_t ino; char *path; };
static struct ident *idents; static size_t nidents, cidents;
static unsigned long ident_conflicts, ident_cross_namespace;
static int first_problem = 1;
static size_t root_len;

static void report(const char *path, const char *name, const char *why)
{
	const char *rel = path + root_len;

	if (*rel == '/')
		rel++;
	printf("%s\n    {\"path\": \"", first_problem ? "" : ",");
	for (; *rel; rel++) {
		if (*rel == '"' || *rel == '\\') putchar('\\');
		if ((unsigned char)*rel < 0x20) printf("\\u%04x", (unsigned char)*rel);
		else putchar(*rel);
	}
	printf("\", \"attribute\": \"%s\", \"problem\": \"%s\"}", name ? name : "", why);
	first_problem = 0;
	problems++;
}

static int visit(const char *path, const struct stat *sb, int flag, struct FTW *ftw)
{
	char *list, *p;
	ssize_t len;
	unsigned long per_file = 0;
	(void)sb; (void)ftw;

	if (flag == FTW_SL || flag == FTW_SLN || flag == FTW_NS || flag == FTW_DNR)
		return 0;
	files++;
	len = llistxattr(path, NULL, 0);
	if (len < 0) {
		if (errno == ENOTSUP || errno == EOPNOTSUPP)
			return 0;
		report(path, NULL, "cannot list attributes");
		return 0;
	}
	if (!len)
		return 0;
	list = malloc((size_t)len);
	if (!list)
		return 1;
	len = llistxattr(path, list, (size_t)len);
	if (len < 0) {
		free(list);
		report(path, NULL, "cannot list attributes");
		return 0;
	}
	for (p = list; p < list + len; p += strlen(p) + 1) {
		ma_u8 value[MOOB_ATTR_VALUE_MAX + 1];
		const char *name = p + MOOB_ATTR_PREFIX_LEN;
		size_t nlen;
		ssize_t n;
		int err;

		if (strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
			continue;
		nlen = strlen(name);
		if (moob_attr_name_reserved(name, nlen)) {
			if (ftw->level != 0)
				report(path, name, "format marker outside the root");
			continue;
		}
		attrs++;
		per_file++;
		if (!moob_attr_name_ok(name, nlen)) {
			report(path, name, moob_attr_strerror(MOOB_ATTR_EBADNAME));
			continue;
		}
		n = lgetxattr(path, p, value, sizeof value);
		if (n < 0) {
			report(path, name, errno == ERANGE ? "value larger than the maximum" : "cannot read value");
			continue;
		}
		err = moob_attr_validate(value, (size_t)n);
		if (err)
			report(path, name, moob_attr_strerror(err));
	}
	if (per_file > MOOB_ATTR_MAX_PER_FILE)
		report(path, NULL, "more typed attributes than the per-file limit");
	free(list);
	{	/* collect (namespace, memory_id) when both are well-formed strings */
		ma_u8 ns[MOOB_ATTR_VALUE_MAX + 1], id[MOOB_ATTR_VALUE_MAX + 1];
		ssize_t a = lgetxattr(path, MOOB_ATTR_PREFIX "namespace", ns, sizeof ns);
		ssize_t b = lgetxattr(path, MOOB_ATTR_PREFIX "memory_id", id, sizeof id);
		if (a > 1 && b > 1 && ns[0] == MOOB_T_STRING && id[0] == MOOB_T_STRING &&
		    !moob_attr_validate(ns, (size_t)a) && !moob_attr_validate(id, (size_t)b)) {
			if (nidents == cidents) {
				cidents = cidents ? cidents * 2 : 256;
				idents = realloc(idents, cidents * sizeof *idents);
				if (!idents) return 1;
			}
			memcpy(idents[nidents].ns, ns + 1, (size_t)a - 1); idents[nidents].ns[a - 1] = 0;
			memcpy(idents[nidents].id, id + 1, (size_t)b - 1); idents[nidents].id[b - 1] = 0;
			idents[nidents].dev = sb->st_dev; idents[nidents].ino = sb->st_ino;
			idents[nidents].path = strdup(path);
			nidents++;
		}
	}
	return 0;
}

static int ident_cmp(const void *x, const void *y)
{
	const struct ident *a = x, *b = y; int c = strcmp(a->id, b->id);
	if (c) return c;
	c = strcmp(a->ns, b->ns);
	if (c) return c;
	return a->ino < b->ino ? -1 : a->ino > b->ino;
}

/* Two distinct inodes with the same (namespace, memory_id) are a conflict; the same inode
 * reached by several paths (hard links) is not; the same id in another namespace is normal. */
static void check_identities(void)
{
	size_t i;

	if (nidents < 2)
		return;		/* nothing to compare; qsort() must not get a null array */
	qsort(idents, nidents, sizeof *idents, ident_cmp);
	for (i = 1; i < nidents; i++) {
		if (strcmp(idents[i].id, idents[i - 1].id)) continue;
		if (strcmp(idents[i].ns, idents[i - 1].ns)) { ident_cross_namespace++; continue; }
		if (idents[i].ino == idents[i - 1].ino && idents[i].dev == idents[i - 1].dev) continue;
		report(idents[i].path, "memory_id", "duplicate (namespace, memory_id) on a different inode");
		ident_conflicts++;
	}
}

int main(int argc, char **argv)
{
	ma_u8 value[16];
	ssize_t n;
	unsigned int version = 0;
	const char *format_state;

	if (argc != 2) {
		fprintf(stderr, "usage: moobfs-check <directory>\n");
		return 64;
	}
	root_len = strlen(argv[1]);
	n = lgetxattr(argv[1], MOOB_ATTR_PREFIX MOOB_ATTR_FORMAT_NAME, value, sizeof value);
	if (n < 0 && errno == ENODATA) {
		format_state = "missing";
	} else if (n < 0) {
		format_state = "unreadable";
	} else {
		version = moob_attr_parse_format(value, (size_t)n);
		format_state = !version ? "invalid" : version > MOOB_ATTR_FORMAT_VERSION ? "newer" : "ok";
	}
	printf("{\n  \"format\": {\"state\": \"%s\", \"version\": %u, \"supported\": %u},\n  \"problems\": [",
	       format_state, version, MOOB_ATTR_FORMAT_VERSION);
	if (strcmp(format_state, "ok") && strcmp(format_state, "missing"))
		report(argv[1], MOOB_ATTR_FORMAT_NAME, format_state);
	if (nftw(argv[1], visit, 32, FTW_PHYS | FTW_MOUNT)) {
		fprintf(stderr, "moobfs-check: %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	check_identities();
	printf("%s],\n  \"files\": %lu,\n  \"typed_attributes\": %lu,\n  \"identities\": %zu,\n"
	       "  \"identity_conflicts\": %lu,\n  \"same_id_other_namespace\": %lu,\n  \"problem_count\": %lu\n}\n",
	       first_problem ? "" : "\n  ", files, attrs, nidents, ident_conflicts, ident_cross_namespace, problems);
	return problems ? 2 : 0;
}
