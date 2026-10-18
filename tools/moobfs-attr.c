/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs-attr: read and write typed MoobFS attributes through the standard
 * xattr API. Works on a MoobFS layer mount (validated by the kernel) and on a
 * plain lower directory (validated here).
 *
 *   moobfs-attr get <file> <name>
 *   moobfs-attr set <file> <name> <string|int64|double|bool|time|raw> <value>
 *   moobfs-attr rm <file> <name>
 *   moobfs-attr list <file>
 *   moobfs-attr rawset <file> <name> <hex>      (test helper: writes bytes unchecked)
 *   moobfs-attr --help
 *
 * Exit status (docs/tools.md): 0 success; 1 a system call failed; 2 get found an invalid
 * value (printed as "invalid"); 3 get found no such attribute; 64 usage; 65 set was given a
 * value that cannot be encoded as the type, or an invalid one.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/xattr.h>
#include "moobfs_attr.h"

static const char synopsis[] =
	"usage: moobfs-attr get <file> <name>\n"
	"       moobfs-attr set <file> <name> <string|int64|double|bool|time|raw> <value>\n"
	"       moobfs-attr rm <file> <name>\n"
	"       moobfs-attr list <file>\n"
	"       moobfs-attr rawset <file> <name> <hex>   (test helper: writes bytes unchecked)\n"
	"       moobfs-attr --help\n";

static int usage(void)
{
	fputs(synopsis, stderr);
	return 64;
}

/* "moobfs-attr: <file>: <reason>", the style of moobfs-check; returns the exit status 1 */
static int sys_fail(const char *file)
{
	fprintf(stderr, "moobfs-attr: %s: %s\n", file, strerror(errno));
	return 1;
}

static int full_name(char *out, size_t cap, const char *name)
{
	size_t len = strlen(name);

	if (!moob_attr_name_ok(name, len)) {
		fprintf(stderr, "moobfs-attr: %s\n", moob_attr_strerror(MOOB_ATTR_EBADNAME));
		return -1;
	}
	snprintf(out, cap, "%s%s", MOOB_ATTR_PREFIX, name);
	return 0;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int encode(ma_u8 *out, size_t *size, const char *type, const char *text)
{
	size_t len = strlen(text);

	if (!strcmp(type, "string")) {
		if (len > MOOB_ATTR_STRING_MAX) return -1;
		out[0] = MOOB_T_STRING; memcpy(out + 1, text, len); *size = len + 1;
	} else if (!strcmp(type, "int64") || !strcmp(type, "time")) {
		char *end; long long v;
		errno = 0; v = strtoll(text, &end, 10);
		if (errno || *end || !len) return -1;
		out[0] = !strcmp(type, "int64") ? MOOB_T_INT64 : MOOB_T_TIME;
		moob_attr_put64(out + 1, (ma_u64)v); *size = 9;
	} else if (!strcmp(type, "double")) {
		char *end; double d; ma_u64 bits;
		errno = 0; d = strtod(text, &end);
		if (errno || *end || !len) return -1;
		memcpy(&bits, &d, 8);
		out[0] = MOOB_T_DOUBLE; moob_attr_put64(out + 1, bits); *size = 9;
	} else if (!strcmp(type, "bool")) {
		out[0] = MOOB_T_BOOL; *size = 2;
		if (!strcmp(text, "true") || !strcmp(text, "1")) out[1] = 1;
		else if (!strcmp(text, "false") || !strcmp(text, "0")) out[1] = 0;
		else return -1;
	} else if (!strcmp(type, "raw")) {
		size_t i;
		if (len % 2 || len / 2 > MOOB_ATTR_RAW_MAX) return -1;
		out[0] = MOOB_T_RAW;
		for (i = 0; i < len / 2; i++) {
			int hi = hexval(text[2 * i]), lo = hexval(text[2 * i + 1]);
			if (hi < 0 || lo < 0) return -1;
			out[1 + i] = (ma_u8)(hi * 16 + lo);
		}
		*size = 1 + len / 2;
	} else {
		return -1;
	}
	return 0;
}

static void print_value(const char *name, const ma_u8 *v, size_t size)
{
	int err = moob_attr_validate(v, size);

	if (err) {
		printf("%s\tinvalid\t%s\n", name, moob_attr_strerror(err));
		return;
	}
	printf("%s\t%s\t", name, moob_attr_type_name(v[0]));
	switch (v[0]) {
	case MOOB_T_STRING: fwrite(v + 1, 1, size - 1, stdout); break;
	case MOOB_T_INT64: case MOOB_T_TIME: printf("%" PRId64, (int64_t)moob_attr_get64(v + 1)); break;
	case MOOB_T_DOUBLE: { ma_u64 bits = moob_attr_get64(v + 1); double d; memcpy(&d, &bits, 8); printf("%.17g", d); break; }
	case MOOB_T_BOOL: fputs(v[1] ? "true" : "false", stdout); break;
	case MOOB_T_RAW: { size_t i; for (i = 1; i < size; i++) printf("%02x", v[i]); break; }
	}
	putchar('\n');
}

int main(int argc, char **argv)
{
	char full[MOOB_ATTR_PREFIX_LEN + MOOB_ATTR_NAME_MAX + 1];
	ma_u8 value[MOOB_ATTR_VALUE_MAX + 1];
	ssize_t n;

	if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
		fputs(synopsis, stdout);
		return 0;
	}
	if (argc < 3)
		return usage();
	if (!strcmp(argv[1], "list")) {
		char *list, *p;
		ssize_t len = listxattr(argv[2], NULL, 0);
		if (len < 0) return sys_fail(argv[2]);
		list = malloc((size_t)len + 1);
		if (!list) return sys_fail(argv[2]);
		len = listxattr(argv[2], list, (size_t)len);
		if (len < 0) return sys_fail(argv[2]);
		for (p = list; p < list + len; p += strlen(p) + 1) {
			if (strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
				continue;
			n = getxattr(argv[2], p, value, sizeof value);
			if (n < 0) { printf("%s\terror\t%s\n", p + MOOB_ATTR_PREFIX_LEN, strerror(errno)); continue; }
			print_value(p + MOOB_ATTR_PREFIX_LEN, value, (size_t)n);
		}
		free(list);
		return 0;
	}
	if (argc < 4 || full_name(full, sizeof full, argv[3]))
		return usage();
	if (!strcmp(argv[1], "get")) {
		n = getxattr(argv[2], full, value, sizeof value);
		if (n < 0) { int e = errno; sys_fail(argv[2]); return e == ENODATA ? 3 : 1; }
		print_value(argv[3], value, (size_t)n);
		return moob_attr_validate(value, (size_t)n) ? 2 : 0;
	}
	if (!strcmp(argv[1], "rm")) {
		if (removexattr(argv[2], full)) return sys_fail(argv[2]);
		return 0;
	}
	if (!strcmp(argv[1], "set") && argc == 6) {
		size_t size;
		int err;
		if (encode(value, &size, argv[4], argv[5])) {
			fprintf(stderr, "moobfs-attr: cannot encode %s as %s\n", argv[5], argv[4]);
			return 65;
		}
		err = moob_attr_validate(value, size);
		if (err) { fprintf(stderr, "moobfs-attr: %s\n", moob_attr_strerror(err)); return 65; }
		if (setxattr(argv[2], full, value, size, 0)) return sys_fail(argv[2]);
		return 0;
	}
	if (!strcmp(argv[1], "rawset") && argc == 5) {
		size_t len = strlen(argv[4]), i;
		if (len % 2 || len / 2 > sizeof value) return usage();
		for (i = 0; i < len / 2; i++) {
			int hi = hexval(argv[4][2 * i]), lo = hexval(argv[4][2 * i + 1]);
			if (hi < 0 || lo < 0) return usage();
			value[i] = (ma_u8)(hi * 16 + lo);
		}
		if (setxattr(argv[2], full, value, len / 2, 0)) return sys_fail(argv[2]);
		return 0;
	}
	return usage();
}
