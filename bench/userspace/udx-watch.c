/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * udx-watch: keeps the user-space baseline index current (docs/history/0092). It watches every
 * directory below <root> with inotify, and turns each event into "look at this path again":
 * the rows of the path are made equal to what is on disk when the batch is applied. Events
 * are therefore only hints; their order, and how many arrive per change, do not matter.
 *
 *   udx-watch [--settle-ms N] <root> <domain depth>
 *
 * Start: every domain is brought up to date by a full pass (as udx-index does), with the
 * watches added before each directory is read, so a change during the pass is not lost.
 * Then events are read in batches; --settle-ms (default 10) is how long it waits after the
 * first event of a batch for more, so the several setxattr calls of one logical write cost
 * one refresh. It is also a floor on how stale an answer can be.
 *
 * Liveness: while it runs it holds an exclusive flock() on every "<domain>/.udx.db", which
 * udx-query --require-live and --status test. When the kernel drops events (IN_Q_OVERFLOW)
 * every domain is first marked state = 'resync', then walked again.
 *
 * It prints "UDX_WATCH_READY ..." on stdout after the first pass and a line of counters on
 * SIGUSR1 and at exit (SIGINT, SIGTERM).
 */
#define _GNU_SOURCE
#include <sys/inotify.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <poll.h>
#include <signal.h>
#include "udx.h"

#define WATCH_MASK (IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | \
		    IN_ONLYDIR | IN_DONT_FOLLOW | IN_EXCL_UNLINK)
#define CACHE 32

struct dom { char *rel; int lockfd; };
struct handle { struct udx_db d; struct dom *owner; unsigned long tick; };
struct dirty { char *path; int rec; };

static char root[PATH_MAX];
static size_t rootlen;
static int depth, ifd, settle_ms = 10;
static char **wdpath;			/* watch descriptor -> directory, relative to root */
static size_t wdcap, nwatches;
static struct dom **doms;
static size_t ndoms, domcap;
static struct handle cache[CACHE];
static unsigned long tick;
static struct dirty *dirty;
static size_t ndirty, dirtycap;
static volatile sig_atomic_t stop, want_stats;
static int link_audit;			/* a domain was dropped: see relink() */
static unsigned long n_events, n_batches, n_refreshed, n_overflows, n_resyncs, max_batch;

static void on_signal(int sig)
{
	if (sig == SIGUSR1)
		want_stats = 1;
	else
		stop = 1;
}

static void stats(void)
{
	struct rusage ru;
	long cpu_ms = 0;

	if (!getrusage(RUSAGE_SELF, &ru))
		cpu_ms = (long)((ru.ru_utime.tv_sec + ru.ru_stime.tv_sec) * 1000 +
				(ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1000);
	fprintf(stderr, "udx-watch: domains=%zu watches=%zu events=%lu batches=%lu refreshed=%lu max_batch=%lu "
		"rows_written=%lu overflows=%lu resyncs=%lu cpu_ms=%ld\n", ndoms, nwatches, n_events, n_batches,
		n_refreshed, max_batch, udx_rows_written, n_overflows, n_resyncs, cpu_ms);
}

/* path relative to root ("" is the root) -> a path the system calls take */
static const char *full_of(const char *rel, char *buf)
{
	size_t n = strlen(rel);

	if (!n)
		return root;
	if (rootlen + 1 + n >= PATH_MAX)
		return "/nonexistent/udx/path-too-long";
	memcpy(buf, root, rootlen);
	buf[rootlen] = '/';
	memcpy(buf + rootlen + 1, rel, n + 1);
	return buf;
}

static int under(const char *path, const char *prefix)
{
	size_t n = strlen(prefix);

	return !n || (!strncmp(path, prefix, n) && (path[n] == '/' || !path[n]));
}

static void add_watch(const char *rel)
{
	char buf[PATH_MAX];
	int wd = inotify_add_watch(ifd, full_of(rel, buf), WATCH_MASK);

	if (wd < 0) {
		if (errno == ENOSPC) {
			fprintf(stderr, "udx-watch: out of inotify watches (fs.inotify.max_user_watches), %zu in use\n", nwatches);
			exit(1);
		}
		return;		/* gone or no longer a directory: its parent will say so */
	}
	if ((size_t)wd >= wdcap) {
		size_t old = wdcap;

		wdpath = udx_grow(wdpath, &wdcap, (size_t)wd + 1, sizeof *wdpath);
		memset(wdpath + old, 0, (wdcap - old) * sizeof *wdpath);
	}
	if (wdpath[wd])
		free(wdpath[wd]);	/* the same inode under a new name */
	else
		nwatches++;
	wdpath[wd] = strdup(rel);
}

static void hook(const char *full)
{
	add_watch(full[rootlen] ? full + rootlen + 1 : "");
}

/* A directory was moved away: its watches would go on reporting under the old name. */
static void drop_watches(const char *prefix)
{
	size_t wd;

	for (wd = 0; wd < wdcap; wd++)
		if (wdpath[wd] && under(wdpath[wd], prefix)) {
			inotify_rm_watch(ifd, (int)wd);
			free(wdpath[wd]);
			wdpath[wd] = NULL;
			nwatches--;
		}
}

static void evict(struct handle *h)
{
	if (h->owner)
		udx_close(&h->d);
	h->owner = NULL;
}

/* The open database of a domain, from a small cache; NULL when it cannot be opened. Only
 * registration creates one: making a file in a directory someone is deleting would stop them. */
static struct udx_db *handle_of(struct dom *dom, int create)
{
	char buf[PATH_MAX];
	struct handle *h, *lru = &cache[0];
	int i;

	for (i = 0; i < CACHE; i++) {
		h = &cache[i];
		if (h->owner == dom) {
			h->tick = ++tick;
			return &h->d;
		}
		if (!h->owner || (lru->owner && h->tick < lru->tick))
			lru = h;
	}
	evict(lru);
	if (udx_open(&lru->d, full_of(dom->rel, buf), create))
		return NULL;
	lru->owner = dom;
	lru->tick = ++tick;
	return &lru->d;
}

static struct dom *find_dom(const char *rel, size_t n)
{
	size_t i;

	for (i = 0; i < ndoms; i++)
		if (!strncmp(doms[i]->rel, rel, n) && !doms[i]->rel[n])
			return doms[i];
	return NULL;
}

static void drop_domains(const char *prefix)
{
	size_t i = 0;
	int c;

	while (i < ndoms) {
		struct dom *dom = doms[i];

		if (!under(dom->rel, prefix)) {
			i++;
			continue;
		}
		for (c = 0; c < CACHE; c++)
			if (cache[c].owner == dom)
				evict(&cache[c]);
		/* only now: closing any descriptor of the file drops SQLite's POSIX locks on it */
		if (dom->lockfd >= 0)
			close(dom->lockfd);
		free(dom->rel);
		free(dom);
		doms[i] = doms[--ndoms];
		link_audit = 1;
	}
}

/* Takes the liveness lock, marks the domain stale, walks it, marks it synced. */
static void register_domain(const char *rel)
{
	char buf[PATH_MAX], path[PATH_MAX + 16];
	struct dom *dom = calloc(1, sizeof *dom);
	struct udx_db *d;
	int tries;

	if (!dom || !(dom->rel = strdup(rel)))
		udx_die("calloc", NULL);
	dom->lockfd = -1;
	doms = udx_grow(doms, &domcap, ndoms + 1, sizeof *doms);
	doms[ndoms++] = dom;
	d = handle_of(dom, 1);
	if (!d) {
		fprintf(stderr, "udx-watch: %s: no database, domain not indexed\n", full_of(rel, buf));
		drop_domains(rel);
		return;
	}
	snprintf(path, sizeof path, "%s/%s", full_of(rel, buf), UDX_DB_NAME);
	dom->lockfd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	for (tries = 0; dom->lockfd >= 0 && flock(dom->lockfd, LOCK_EX | LOCK_NB); tries++) {
		if (tries == 100) {	/* a reader holds it for microseconds; two seconds is another watcher */
			fprintf(stderr, "udx-watch: %s is held by another udx-watch\n", path);
			exit(1);
		}
		usleep(20000);
	}
	udx_exec(d->db, "BEGIN IMMEDIATE");
	udx_meta_text(d, "state", "resync");
	udx_exec(d->db, "COMMIT");
	udx_exec(d->db, "BEGIN IMMEDIATE");
	udx_sync_all(d);
	udx_meta_text(d, "state", "synced");
	udx_meta_int(d, "synced_ns", udx_now_ns());
	udx_exec(d->db, "COMMIT");
}

/* Directories above the domain depth carry no rows: watch them and find the domains below. */
static void scan_above(const char *rel, int ncomp)
{
	char buf[PATH_MAX], child[PATH_MAX];
	struct dirent *e;
	DIR *dp;

	add_watch(rel);
	if (ncomp == depth) {
		register_domain(rel);
		return;
	}
	dp = opendir(full_of(rel, buf));
	if (!dp)
		return;
	while ((e = readdir(dp))) {
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if ((size_t)snprintf(child, sizeof child, "%s%s%s", rel, *rel ? "/" : "", e->d_name) >= sizeof child - rootlen - 2)
			continue;
		if (!lstat(full_of(child, buf), &st) && S_ISDIR(st.st_mode))
			scan_above(child, ncomp + 1);
	}
	closedir(dp);
}

/* Something happened to a directory at or above the domain depth: start that part again. */
static void refresh_above(const char *rel, int ncomp)
{
	char buf[PATH_MAX];
	struct stat st;

	drop_domains(rel);
	if (!lstat(full_of(rel, buf), &st) && S_ISDIR(st.st_mode))
		scan_above(rel, ncomp);
}

static void add_dirty(const char *dir, const char *name, int rec)
{
	char *p;

	if (asprintf(&p, "%s%s%s", dir, *dir && *name ? "/" : "", name) < 0)
		udx_die("asprintf", NULL);
	dirty = udx_grow(dirty, &dirtycap, ndirty + 1, sizeof *dirty);
	dirty[ndirty].path = p;
	dirty[ndirty++].rec = rec;
}

static int dirty_cmp(const void *a, const void *b)
{
	return strcmp(((const struct dirty *)a)->path, ((const struct dirty *)b)->path);
}

/* Where the domain part of a path ends; *ncomp gets the number of components. */
static size_t split(const char *path, int *ncomp)
{
	size_t i, cut = strlen(path);
	int n = *path ? 1 : 0;

	for (i = 0; path[i]; i++)
		if (path[i] == '/' && n++ == depth)
			cut = i;
	*ncomp = n;
	return depth ? cut : 0;
}

/*
 * The other names of inodes whose link count moved, in whatever domain they are. With audit,
 * every name recorded with more than one link is looked at again instead: a domain went away
 * and the rows that would have said which inodes lost a name went with it.
 */
static void relink(int audit)
{
	uint64_t *inos = udx_links;
	size_t n = audit ? 1 : udx_nlinks, i, j, k;

	udx_links = NULL;
	udx_nlinks = udx_links_cap = 0;
	udx_track_links = 0;
	for (i = 0; i < n; i++) {
		for (j = 0; j < ndoms; j++) {
			struct udx_db *d = handle_of(doms[j], 0);
			sqlite3_stmt *st;
			char **paths = NULL;
			size_t np = 0, cap = 0;

			if (!d)
				continue;
			st = d->st[audit ? UDX_S_SEL_LINKED : UDX_S_SEL_INO];
			if (!audit)
				sqlite3_bind_int64(st, 1, (sqlite3_int64)inos[i]);
			while (sqlite3_step(st) == SQLITE_ROW) {
				paths = udx_grow(paths, &cap, np + 1, sizeof *paths);
				paths[np++] = strndup(sqlite3_column_blob(st, 0), (size_t)sqlite3_column_bytes(st, 0));
			}
			sqlite3_reset(st);
			if (!np)
				continue;
			udx_exec(d->db, "BEGIN IMMEDIATE");
			for (k = 0; k < np; k++) {
				udx_refresh(d, paths[k], 0);
				free(paths[k]);
			}
			udx_exec(d->db, "COMMIT");
			free(paths);
		}
	}
	free(inos);
	udx_track_links = 1;
}

static void resync_all(void)
{
	size_t i;

	n_resyncs++;
	/* first, committed on its own, so a reader can tell before the long pass is over */
	for (i = 0; i < ndoms; i++) {
		struct udx_db *d = handle_of(doms[i], 0);

		if (!d)
			continue;
		udx_exec(d->db, "BEGIN IMMEDIATE");
		udx_meta_text(d, "state", "resync");
		udx_exec(d->db, "UPDATE meta SET value = value + 1 WHERE key = 'overflows'");
		udx_exec(d->db, "COMMIT");
	}
	refresh_above("", 0);
}

static void apply(void)
{
	struct udx_db *open_d = NULL;
	char buf[PATH_MAX];
	size_t i, w = 0;

	if (!ndirty)
		return;
	n_batches++;
	qsort(dirty, ndirty, sizeof *dirty, dirty_cmp);
	for (i = 0; i < ndirty; i++) {		/* one refresh per path, however many events */
		if (w && !strcmp(dirty[w - 1].path, dirty[i].path)) {
			dirty[w - 1].rec |= dirty[i].rec;
			free(dirty[i].path);
		} else {
			dirty[w++] = dirty[i];
		}
	}
	if (w > max_batch)
		max_batch = w;
	for (i = 0; i < w; i++) {
		const char *path = dirty[i].path, *rel;
		struct udx_db *d;
		struct dom *dom;
		struct stat st;
		int ncomp;
		size_t cut = split(path, &ncomp);

		if (ncomp <= depth) {
			/* a change of mode on a directory above the domains changes nothing inside them */
			if (ncomp == depth || dirty[i].rec) {
				if (open_d)
					udx_exec(open_d->db, "COMMIT");
				open_d = NULL;
				refresh_above(path, ncomp);
			}
			continue;
		}
		dom = find_dom(path, cut);
		if (!dom)
			continue;
		rel = path + cut + (depth ? 1 : 0);
		if (udx_is_db_name(rel) && !strchr(rel, '/')) {
			/* our own files. A database someone removed is not made again here (the
			 * directory may be on its way out, and a new file would stop its rmdir): the
			 * domain stays unindexed until udx-index or a restart, and a query fails */
			if (!strcmp(rel, UDX_DB_NAME) && lstat(full_of(path, buf), &st)) {
				char *domrel = strndup(path, cut);

				if (open_d)
					udx_exec(open_d->db, "COMMIT");
				open_d = NULL;
				fprintf(stderr, "udx-watch: %s was removed, domain no longer indexed\n", full_of(path, buf));
				drop_domains(domrel);
				free(domrel);
			}
			continue;
		}
		d = handle_of(dom, 0);
		if (!d)
			continue;
		if (d != open_d) {
			if (open_d)
				udx_exec(open_d->db, "COMMIT");
			udx_exec(d->db, "BEGIN IMMEDIATE");
			open_d = d;
		}
		udx_refresh(d, rel, dirty[i].rec);
		n_refreshed++;
	}
	if (open_d)
		udx_exec(open_d->db, "COMMIT");
	for (i = 0; i < w; i++)
		free(dirty[i].path);
	ndirty = 0;
	if (link_audit)
		relink(1);
	else if (udx_nlinks)
		relink(0);
	link_audit = 0;
}

/* Reads what is queued; returns 1 when the kernel said it dropped events. */
static int drain(void)
{
	char buf[65536] __attribute__((aligned(__alignof__(struct inotify_event))));
	ssize_t n;
	int overflow = 0;

	/* bounded, so a writer that never pauses cannot keep the batch from being applied */
	while (ndirty < 100000 && (n = read(ifd, buf, sizeof buf)) > 0) {
		char *p = buf;

		while (p < buf + n) {
			const struct inotify_event *e = (const struct inotify_event *)p;
			const char *dir;

			p += sizeof *e + e->len;
			n_events++;
			if (e->mask & IN_Q_OVERFLOW) {
				overflow = 1;
				continue;
			}
			if (e->wd < 0 || (size_t)e->wd >= wdcap || !(dir = wdpath[e->wd]))
				continue;	/* a watch we already gave up */
			if (e->mask & IN_IGNORED) {
				free(wdpath[e->wd]);
				wdpath[e->wd] = NULL;
				nwatches--;
				continue;
			}
			if (!e->len) {
				/* the root has no parent watch to report its own change of mode */
				if ((e->mask & IN_ATTRIB) && !*dir)
					add_dirty("", "", 0);
				continue;
			}
			if ((e->mask & IN_ISDIR) && (e->mask & IN_MOVED_FROM)) {
				char *moved;

				if (asprintf(&moved, "%s%s%s", dir, *dir ? "/" : "", e->name) < 0)
					udx_die("asprintf", NULL);
				drop_watches(moved);
				free(moved);
				dir = wdpath[e->wd];
				if (!dir)
					continue;
			}
			add_dirty(dir, e->name, (e->mask & IN_ISDIR) && !(e->mask & IN_ATTRIB));
		}
	}
	return overflow;
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = on_signal };
	struct rlimit rl;
	struct pollfd pfd;
	char *end;
	long v;

	while (argc >= 3 && !strcmp(argv[1], "--settle-ms")) {
		settle_ms = atoi(argv[2]);
		argv += 2; argc -= 2;
	}
	if (argc != 3) {
		fprintf(stderr, "usage: udx-watch [--settle-ms N] <root> <domain depth>\n");
		return 64;
	}
	v = strtol(argv[2], &end, 10);
	rootlen = strlen(argv[1]);
	if (*end || v < 0 || v > 64 || !rootlen || rootlen >= PATH_MAX / 2 || settle_ms < 0)
		return 64;
	depth = (int)v;
	memcpy(root, argv[1], rootlen + 1);
	while (rootlen > 1 && root[rootlen - 1] == '/')
		root[--rootlen] = 0;
	/* one descriptor per domain for the lock, plus the cache and the walk */
	if (!getrlimit(RLIMIT_NOFILE, &rl)) {
		rl.rlim_cur = rl.rlim_max;
		setrlimit(RLIMIT_NOFILE, &rl);
	}
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0)
		udx_die("inotify_init1", NULL);
	udx_dir_hook = hook;
	refresh_above("", 0);
	udx_nlinks = 0;		/* a full pass has already looked at every name */
	link_audit = 0;
	udx_track_links = 1;
	printf("UDX_WATCH_READY domains=%zu watches=%zu settle_ms=%d\n", ndoms, nwatches, settle_ms);
	fflush(stdout);

	pfd.fd = ifd;
	pfd.events = POLLIN;
	while (!stop) {
		int overflow;

		if (want_stats) {
			want_stats = 0;
			stats();
		}
		if (poll(&pfd, 1, 500) <= 0)
			continue;
		if (settle_ms)
			usleep((useconds_t)settle_ms * 1000);
		overflow = drain();
		if (overflow) {
			size_t i;

			n_overflows++;
			for (i = 0; i < ndirty; i++)
				free(dirty[i].path);
			ndirty = 0;
			resync_all();
			udx_nlinks = 0;
			link_audit = 0;
		} else {
			apply();
		}
	}
	stats();
	return 0;
}
