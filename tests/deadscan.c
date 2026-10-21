/* SPDX-License-Identifier: GPL-2.0-only */
/* R2-10 (journal 0170): remove a scanned subdirectory after its names were
 * listed. Function tracing synchronizes the removal with the second index_load
 * (the first file, after the directory itself). No production test hook added.
 * Dedicated VM only; requires function tracing and scan_delay_us. */
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static void put(const char *name, const char *value)
{
	char path[256];
	int fd;
	size_t len = strlen(value);

	snprintf(path, sizeof path, "/sys/kernel/tracing/%s", name);
	fd = open(path, O_WRONLY | O_TRUNC);
	if (fd < 0 || write(fd, value, len) != (ssize_t)len) die(path);
	close(fd);
}

static int calls(void)
{
	char line[1024];
	int n = 0;
	FILE *fp = fopen("/sys/kernel/tracing/trace", "r");

	if (!fp) die("trace");
	while (fgets(line, sizeof line, fp))
		if (strstr(line, " index_load ") || strstr(line, " index_load <-")) n++;
	fclose(fp);
	return n;
}

int main(int argc, char **argv)
{
	char sub[1024], files[2][1100], pidstr[32], c;
	int fd, go[2], status, i, seen = 0;
	pid_t pid;

	if (argc != 2) { fprintf(stderr, "usage: deadscan <fresh domain>\n"); return 2; }
	setbuf(stdout, NULL);
	if (mkdir(argv[1], 0755)) die("domain");
	snprintf(sub, sizeof sub, "%s/sub", argv[1]);
	if (mkdir(sub, 0755)) die("subdir");
	for (i = 0; i < 2; i++) {
		snprintf(files[i], sizeof files[i], "%s/f%d", sub, i);
		fd = open(files[i], O_CREAT | O_EXCL | O_WRONLY, 0644);
		if (fd < 0) die("create");
		close(fd);
		if (setxattr(files[i], "user.moob.kind", "\1note", 5, 0)) die("attribute");
	}
	if (pipe(go)) die("pipe");
	pid = fork();
	if (pid < 0) die("fork");
	if (!pid) {
		struct moob_query_result r[8];
		struct moob_query2 q = {0};
		int rc, saved;

		close(go[1]);
		if (read(go[0], &c, 1) != 1) die("go");
		fd = open(argv[1], O_RDONLY | O_DIRECTORY);
		if (fd < 0) die("open domain");
		q.version = MOOB_QUERY2_VERSION;
		q.nterms = 1; q.limit = 8;
		q.results = (uint64_t)(uintptr_t)r;
		strcpy(q.terms[0].name, "kind");
		q.terms[0].op = MOOB_OP_EXISTS;
		rc = ioctl(fd, MOOB_IOC_QUERY2, &q);
		saved = errno;
		printf("DEADSCAN_RESULT rc=%d errno=%d count=%u\n", rc, rc ? saved : 0, q.count);
		_exit(rc || q.count ? 1 : 0);
	}
	close(go[0]);
	put("tracing_on", "0");
	put("current_tracer", "nop");
	put("set_ftrace_filter", "index_load");
	snprintf(pidstr, sizeof pidstr, "%ld", (long)pid);
	put("set_ftrace_pid", pidstr);
	put("trace", "");
	put("current_tracer", "function");
	put("tracing_on", "1");
	if (write(go[1], "x", 1) != 1) die("start");
	close(go[1]);
	for (i = 0; i < 10000; i++) {
		seen = calls();
		if (seen >= 2) break;
		usleep(1000);
	}
	if (seen < 2) die("scan did not reach the first listed file");
	for (i = 0; i < 2; i++)
		if (unlink(files[i])) die("remove file");
	if (rmdir(sub)) die("remove scanned directory");
	printf("DEADSCAN_REMOVED after_index_load_calls=%d\n", seen);
	if (waitpid(pid, &status, 0) != pid) die("wait");
	put("tracing_on", "0");
	put("current_tracer", "nop");
	put("set_ftrace_filter", "");
	put("set_ftrace_pid", "");
	return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
}
