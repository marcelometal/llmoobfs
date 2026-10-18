/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * fusestall: a FUSE filesystem whose daemon answers FUSE_INIT and nothing else (journal 0116).
 * Any lookup or getattr on it waits until the daemon goes away; once the daemon has read the
 * request, the wait cannot be interrupted (request_wait_answer, fs/fuse/dev.c). It stands in
 * for a user who mounts FUSE over a directory of a domain to make the layer wait.
 *
 *   fusestall <mount point> <ready file>
 *
 * Mounts on <mount point>, creates <ready file> once INIT is answered, and reads requests
 * without answering them until killed; its exit closes /dev/fuse, which aborts the
 * connection and ends every wait. No libfuse: the protocol of <linux/fuse.h> directly.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

static char buf[FUSE_MIN_READ_BUFFER + 65536];

int main(int argc, char **argv)
{
	char opts[128];
	int fd, ready = 0;

	if (argc != 3)
		return 2;
	fd = open("/dev/fuse", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("/dev/fuse");
		return 2;
	}
	snprintf(opts, sizeof opts, "fd=%d,rootmode=40000,user_id=0,group_id=0,allow_other", fd);
	if (mount("fusestall", argv[1], "fuse", MS_NOSUID | MS_NODEV, opts)) {
		perror("mount");
		return 2;
	}
	for (;;) {
		struct fuse_in_header *in = (struct fuse_in_header *)buf;
		ssize_t n = read(fd, buf, sizeof buf);

		if (n < 0 && (errno == EINTR || errno == EAGAIN))
			continue;
		if (n < (ssize_t)sizeof(*in))
			return n < 0 ? 1 : 0;	/* ENODEV: unmounted */
		if (in->opcode == FUSE_INIT) {
			struct {
				struct fuse_out_header h;
				struct fuse_init_out i;
			} out;

			memset(&out, 0, sizeof out);
			out.h.len = sizeof out;
			out.h.unique = in->unique;
			out.i.major = FUSE_KERNEL_VERSION;
			out.i.minor = FUSE_KERNEL_MINOR_VERSION;
			out.i.max_write = 4096;
			if (write(fd, &out, sizeof out) != (ssize_t)sizeof out) {
				perror("INIT reply");
				return 1;
			}
			if (!ready) {
				int r = open(argv[2], O_CREAT | O_WRONLY | O_CLOEXEC, 0644);

				if (r < 0 || close(r))
					return 1;
				ready = 1;
			}
		}
		/* every other request is read and never answered */
	}
}
