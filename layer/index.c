// SPDX-License-Identifier: GPL-2.0-only
/*
 * In-memory attribute index of the MoobFS memory layer (phase 2d, domains in Q2a).
 *
 * Built at mount time by walking the lower tree and reading every
 * user.moob.* attribute; kept current by the xattr, unlink, rmdir, rename,
 * setattr, set_acl and link paths of main.c; never persisted. The index is
 * partitioned by permission domain (docs/spec-query-v2.md): a partition is the
 * set of entries of one attribute name in one domain, split again by whether
 * the inode passes the homogeneity rule. Answers MOOB_IOC_QUERY (all
 * partitions; privileged when domains are on) and MOOB_IOC_GET_INFO on the
 * mount root, and MOOB_IOC_QUERY2 and MOOB_IOC_QUERY2_BATCH (one domain) on a
 * domain directory.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/xattr.h>
#include <linux/uaccess.h>
#include <linux/dcache.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/uio.h>
#include <linux/limits.h>
#include <linux/posix_acl.h>
#include <linux/capability.h>
#include <linux/random.h>
#include <linux/siphash.h>
#include <linux/sched/mm.h>
#include "moobl.h"
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

/*
 * The index lock is taken when a layer inode is evicted (moobl_index_inode_evicted), and
 * memory reclaim evicts inodes. An allocation made with the lock held must therefore not
 * recurse into filesystem reclaim, or the task would wait for the lock it holds. Every
 * section runs in a NOFS scope, so the allocations inside it can stay GFP_KERNEL.
 */
static inline unsigned int ix_write_lock(struct moobl_index *ix)
{
	down_write(&ix->lock);
	return memalloc_nofs_save();
}

static inline void ix_write_unlock(struct moobl_index *ix, unsigned int nofs)
{
	memalloc_nofs_restore(nofs);
	up_write(&ix->lock);
}

static inline unsigned int ix_read_lock(struct moobl_index *ix)
{
	down_read(&ix->lock);
	return memalloc_nofs_save();
}

static inline void ix_read_unlock(struct moobl_index *ix, unsigned int nofs)
{
	memalloc_nofs_restore(nofs);
	up_read(&ix->lock);
}

/* One partition: ordered in ix->names by (name, domain, hetero). */
struct moobl_ix_name {
	struct rb_node node;
	struct rb_root entries;		/* struct moobl_ix_entry, ordered by (type, value, ino) */
	u32 count;
	u64 domain;			/* lower ino of the domain directory; 0: outside any domain */
	bool hetero;			/* inodes failing the homogeneity rule */
	char name[MOOB_ATTR_NAME_MAX + 1];
};

struct moobl_ix_inode {
	u64 ino;
	struct list_head entries;	/* this inode's typed attributes */
	struct inode *upper;		/* cached layer inode, or NULL */
	char *path;			/* mount-relative path recorded at scan or write time */
	u64 domain;			/* partition key of all its entries */
	bool hetero;
};

/* Where an inode's entries belong (docs/spec-query-v2.md §2, §3). */
struct moobl_place {
	u64 domain;
	bool hetero;
};

struct moobl_ix_entry {
	struct rb_node node;
	struct list_head by_ino;
	struct moobl_ix_name *name;
	struct moobl_ix_inode *owner;
	u8 type;
	u16 len;			/* payload length, type byte excluded */
	u8 value[];
};

static inline struct moobl_index *IX(struct super_block *sb)
{
	return &MOOBL_SB(sb)->index;
}

/* ---- ordering ---------------------------------------------------------- */

static u64 double_key(const u8 *payload)
{
	u64 bits = moob_attr_get64(payload);

	/* Maps IEEE 754 binary64 to an unsigned key with the same ordering. */
	return (bits & (1ULL << 63)) ? ~bits : (bits | (1ULL << 63));
}

/* Compares a stored entry with (type, payload, len); ino breaks ties on request. */
static int value_cmp(u8 atype, const u8 *a, u16 alen, u8 btype, const u8 *b, u16 blen)
{
	if (atype != btype)
		return atype < btype ? -1 : 1;
	switch (atype) {
	case MOOB_T_INT64:
	case MOOB_T_TIME: {
		s64 x = (s64)moob_attr_get64(a), y = (s64)moob_attr_get64(b);

		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_DOUBLE: {
		u64 x = double_key(a), y = double_key(b);

		return x < y ? -1 : x > y ? 1 : 0;
	}
	case MOOB_T_BOOL:
		return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
	default: {
		int c = memcmp(a, b, min(alen, blen));

		if (c)
			return c;
		return alen < blen ? -1 : alen > blen ? 1 : 0;
	}
	}
}

static int entry_cmp(const struct moobl_ix_entry *e, u8 type, const u8 *v, u16 len, u64 ino)
{
	int c = value_cmp(e->type, e->value, e->len, type, v, len);

	if (c)
		return c;
	return e->owner->ino < ino ? -1 : e->owner->ino > ino ? 1 : 0;
}

/* ---- accounting and lookup --------------------------------------------- */

static int charge(struct moobl_index *ix, size_t bytes)
{
	if (ix->used + bytes > ix->budget)
		return -ENOMEM;
	ix->used += bytes;
	return 0;
}

static void uncharge(struct moobl_index *ix, size_t bytes)
{
	ix->used -= min_t(u64, ix->used, bytes);
}

static int part_cmp(const char *name, u64 domain, bool hetero, const struct moobl_ix_name *nm)
{
	int c = strcmp(name, nm->name);

	if (c)
		return c;
	if (domain != nm->domain)
		return domain < nm->domain ? -1 : 1;
	return (int)hetero - (int)nm->hetero;
}

static struct moobl_ix_name *name_find(struct moobl_index *ix, const char *name, u64 domain,
				       bool hetero)
{
	struct rb_node *n = ix->names.rb_node;

	lockdep_assert_held(&ix->lock);
	while (n) {
		struct moobl_ix_name *nm = rb_entry(n, struct moobl_ix_name, node);
		int c = part_cmp(name, domain, hetero, nm);

		if (!c)
			return nm;
		n = c < 0 ? n->rb_left : n->rb_right;
	}
	return NULL;
}

/* First partition of this name at or after (domain, hetero), or NULL. */
static struct moobl_ix_name *name_lower_bound(struct moobl_index *ix, const char *name, u64 domain,
					      bool hetero)
{
	struct rb_node *n = ix->names.rb_node;
	struct moobl_ix_name *best = NULL;

	while (n) {
		struct moobl_ix_name *nm = rb_entry(n, struct moobl_ix_name, node);

		if (part_cmp(name, domain, hetero, nm) <= 0) {
			best = nm;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best && !strcmp(best->name, name) ? best : NULL;
}

static struct moobl_ix_name *name_get(struct moobl_index *ix, const char *name, u64 domain,
				      bool hetero)
{
	struct rb_node **p = &ix->names.rb_node, *parent = NULL;
	struct moobl_ix_name *nm;

	lockdep_assert_held_write(&ix->lock);
	while (*p) {
		int c;

		parent = *p;
		nm = rb_entry(parent, struct moobl_ix_name, node);
		c = part_cmp(name, domain, hetero, nm);
		if (!c)
			return nm;
		p = c < 0 ? &parent->rb_left : &parent->rb_right;
	}
	if (charge(ix, sizeof(*nm)))
		return ERR_PTR(-ENOMEM);
	nm = kzalloc(sizeof(*nm), GFP_KERNEL);
	if (!nm) {
		uncharge(ix, sizeof(*nm));
		return ERR_PTR(-ENOMEM);
	}
	strscpy(nm->name, name, sizeof(nm->name));
	nm->domain = domain;
	nm->hetero = hetero;
	nm->entries = RB_ROOT;
	rb_link_node(&nm->node, parent, p);
	rb_insert_color(&nm->node, &ix->names);
	return nm;
}

static void name_put_if_empty(struct moobl_index *ix, struct moobl_ix_name *nm)
{
	if (nm->count)
		return;
	rb_erase(&nm->node, &ix->names);
	kfree(nm);
	uncharge(ix, sizeof(*nm));
}

static struct moobl_ix_inode *inode_rec_get(struct moobl_index *ix, u64 ino)
{
	struct moobl_ix_inode *rec = xa_load(&ix->inodes, ino);

	lockdep_assert_held_write(&ix->lock);
	if (rec)
		return rec;
	if (charge(ix, sizeof(*rec)))
		return ERR_PTR(-ENOMEM);
	rec = kzalloc(sizeof(*rec), GFP_KERNEL);
	if (!rec) {
		uncharge(ix, sizeof(*rec));
		return ERR_PTR(-ENOMEM);
	}
	rec->ino = ino;
	INIT_LIST_HEAD(&rec->entries);
	if (xa_err(xa_store(&ix->inodes, ino, rec, GFP_KERNEL))) {
		kfree(rec);
		uncharge(ix, sizeof(*rec));
		return ERR_PTR(-ENOMEM);
	}
	ix->inode_count++;
	return rec;
}

static void inode_rec_put_if_empty(struct moobl_index *ix, struct moobl_ix_inode *rec)
{
	if (!list_empty(&rec->entries))
		return;
	if (rec->hetero)
		ix->hetero_count--;
	xa_erase(&ix->inodes, rec->ino);
	if (rec->path)
		uncharge(ix, strlen(rec->path) + 1);
	kfree(rec->path);
	kfree(rec);
	uncharge(ix, sizeof(*rec));
	ix->inode_count--;
}

static void entry_free(struct moobl_index *ix, struct moobl_ix_entry *e)
{
	lockdep_assert_held_write(&ix->lock);
	rb_erase(&e->node, &e->name->entries);
	list_del(&e->by_ino);
	e->name->count--;
	ix->entries--;
	uncharge(ix, sizeof(*e) + e->len);
	name_put_if_empty(ix, e->name);
	kfree(e);
}

static struct moobl_ix_entry *entry_find(struct moobl_ix_inode *rec, const char *name)
{
	struct moobl_ix_entry *e;

	list_for_each_entry(e, &rec->entries, by_ino)
		if (!strcmp(e->name->name, name))
			return e;
	return NULL;
}

/* The entry of rec under one name node of rec's partition (NULL node: none carries it). */
static struct moobl_ix_entry *entry_find_node(struct moobl_ix_inode *rec,
					      const struct moobl_ix_name *nm)
{
	struct moobl_ix_entry *e;

	if (!nm)
		return NULL;
	list_for_each_entry(e, &rec->entries, by_ino)
		if (e->name == nm)
			return e;
	return NULL;
}

static void entry_link(struct moobl_ix_name *nm, struct moobl_ix_entry *e, u64 ino)
{
	struct rb_node **p = &nm->entries.rb_node, *parent = NULL;

	while (*p) {
		struct moobl_ix_entry *cur;

		parent = *p;
		cur = rb_entry(parent, struct moobl_ix_entry, node);
		p = entry_cmp(cur, e->type, e->value, e->len, ino) < 0 ? &parent->rb_right
								       : &parent->rb_left;
	}
	rb_link_node(&e->node, parent, p);
	rb_insert_color(&e->node, &nm->entries);
}

/*
 * Moves every entry of an inode to the partition given by pl. An entry that cannot be
 * moved for lack of memory is dropped and the index marked incomplete, so it can never be
 * answered from a partition it no longer belongs to. Caller holds the write lock.
 */
static int rec_set_partition(struct moobl_index *ix, struct moobl_ix_inode *rec,
			     const struct moobl_place *pl)
{
	struct moobl_ix_entry *e, *tmp;
	int err = 0;

	if (rec->domain == pl->domain && rec->hetero == pl->hetero)
		return 0;
	list_for_each_entry_safe(e, tmp, &rec->entries, by_ino) {
		struct moobl_ix_name *old = e->name, *nm;

		rb_erase(&e->node, &old->entries);
		old->count--;
		nm = name_get(ix, old->name, pl->domain, pl->hetero);
		if (IS_ERR(nm)) {
			list_del(&e->by_ino);
			ix->entries--;
			uncharge(ix, sizeof(*e) + e->len);
			kfree(e);
			name_put_if_empty(ix, old);
			err = -ENOMEM;
			continue;
		}
		e->name = nm;
		entry_link(nm, e, rec->ino);
		nm->count++;
		name_put_if_empty(ix, old);
	}
	if (rec->hetero != pl->hetero) {
		if (pl->hetero)
			ix->hetero_count++;
		else
			ix->hetero_count--;
	}
	rec->domain = pl->domain;
	rec->hetero = pl->hetero;
	if (err)
		ix->incomplete = true;
	return err;
}

/* Replaces or inserts one typed attribute. Caller holds the write lock. */
static int index_put(struct moobl_index *ix, u64 ino, const struct moobl_place *pl,
		     const char *name, const u8 *value, size_t size)
{
	struct moobl_ix_inode *rec;
	struct moobl_ix_name *nm;
	struct moobl_ix_entry *e, *old;
	u16 len = (u16)(size - 1);

	lockdep_assert_held_write(&ix->lock);
	if (value[0] == MOOB_T_RAW)
		return 0;	/* raw payloads are stored but never indexed */
	rec = inode_rec_get(ix, ino);
	if (IS_ERR(rec))
		return PTR_ERR(rec);
	rec_set_partition(ix, rec, pl);
	old = entry_find(rec, name);
	if (old)
		entry_free(ix, old);	/* before name_get: freeing may drop the name node */
	nm = name_get(ix, name, rec->domain, rec->hetero);
	if (IS_ERR(nm)) {
		inode_rec_put_if_empty(ix, rec);
		return PTR_ERR(nm);
	}
	if (charge(ix, sizeof(*e) + len)) {
		name_put_if_empty(ix, nm);
		inode_rec_put_if_empty(ix, rec);
		return -ENOMEM;
	}
	e = kmalloc(sizeof(*e) + len, GFP_KERNEL);
	if (!e) {
		uncharge(ix, sizeof(*e) + len);
		name_put_if_empty(ix, nm);
		inode_rec_put_if_empty(ix, rec);
		return -ENOMEM;
	}
	e->name = nm;
	e->owner = rec;
	e->type = value[0];
	e->len = len;
	memcpy(e->value, value + 1, len);
	entry_link(nm, e, ino);
	list_add(&e->by_ino, &rec->entries);
	nm->count++;
	ix->entries++;
	return 0;
}

static void index_set_path(struct moobl_index *ix, struct moobl_ix_inode *rec, const char *path)
{
	size_t len = strlen(path) + 1;
	char *copy;

	if (rec->path && !strcmp(rec->path, path))
		return;
	if (charge(ix, len))
		return;	/* keep the old name; QUERY will fall back to the dentry cache */
	copy = kmemdup(path, len, GFP_KERNEL);
	if (!copy) {
		uncharge(ix, len);
		return;
	}
	if (rec->path)
		uncharge(ix, strlen(rec->path) + 1);
	kfree(rec->path);
	rec->path = copy;
}

/* ---- domains and the homogeneity rule (docs/spec-query-v2.md §2, §3) ---- */

#define MOOBL_MAX_DEPTH 64

/* True when the inode carries an access ACL beyond the minimal one (mode bits only). */
static bool ext_acl(struct inode *inode)
{
	struct posix_acl *acl;
	bool ext;

	if (!IS_POSIXACL(inode))
		return false;
	acl = get_inode_acl(inode, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return true;	/* unknown: treat as restrictive */
	if (!acl)
		return false;
	ext = acl->a_count > 3;
	posix_acl_release(acl);
	return ext;
}

/*
 * Sufficient condition for "everyone who can traverse the domain directory dom can also
 * reach in": read it (want_read, a file or a directory carrying attributes) or traverse it
 * (an intermediate directory). Either in grants the class bit to others and has no
 * extended ACL, or in and dom share owner and group, neither has an extended ACL, and in
 * grants the bit to every class that dom lets traverse. Lower inodes: permissions are the
 * lower filesystem's.
 */
static bool reach_ok(struct inode *in, struct inode *dom, bool want_read)
{
	/* octal class bits: read 0444 or search 0111, split per owner, group, other */
	umode_t u = want_read ? 0400 : 0100, g = want_read ? 0040 : 0010;
	umode_t o = want_read ? 0004 : 0001;
	bool acl = ext_acl(in);

	if (!acl && (in->i_mode & o))
		return true;
	if (acl || ext_acl(dom))
		return false;
	if (!uid_eq(in->i_uid, dom->i_uid) || !gid_eq(in->i_gid, dom->i_gid))
		return false;
	if ((dom->i_mode & 0100) && !(in->i_mode & u))
		return false;
	if ((dom->i_mode & 0010) && !(in->i_mode & g))
		return false;
	if ((dom->i_mode & 0001) && !(in->i_mode & o))
		return false;
	return true;
}

static bool target_ok(struct inode *in, struct inode *dom)
{
	if (S_ISREG(in->i_mode) && in->i_nlink > 1)
		return false;	/* another name may live in another domain */
	return reach_ok(in, dom, true);
}

/*
 * Walks from a layer dentry up to the mount root, keeping a reference on every dentry on
 * the way: chain[0] is the dentry itself (depth *depth), chain[*depth - j] is its ancestor
 * at depth j. Returns false when the path is deeper than MOOBL_MAX_DEPTH (references
 * dropped). Directory renames never change a dentry's depth or domain (EXDEV otherwise),
 * so a concurrent rename cannot move it between partitions behind the caller's back.
 */
static bool walk_up(struct dentry *dentry, struct dentry **chain, unsigned int *depth)
{
	struct dentry *d = dget(dentry);
	unsigned int k = 0;

	while (!IS_ROOT(d)) {
		if (k == MOOBL_MAX_DEPTH) {
			dput(d);
			while (k)
				dput(chain[--k]);
			return false;
		}
		chain[k++] = d;
		d = dget_parent(d);
	}
	dput(d);
	*depth = k;
	return true;
}

static void chain_put(struct dentry **chain, unsigned int k)
{
	while (k)
		dput(chain[--k]);
}

/*
 * Partition of a layer dentry that carries attributes. With self, the inode is placed at the
 * name of dentry (a rename target, before the VFS moves the dentry). May sleep (ACL reads).
 */
static void place_dentry(struct moobl_index *ix, struct dentry *dentry, struct inode *self,
			 struct moobl_place *pl)
{
	struct dentry **chain;
	unsigned int n = ix->domain_depth, k, i;
	struct inode *dom;
	bool ok = true;

	pl->domain = 0;
	pl->hetero = false;
	if (!n)
		return;
	chain = kmalloc_array(MOOBL_MAX_DEPTH, sizeof(*chain), GFP_KERNEL);
	if (!chain || !walk_up(dentry, chain, &k)) {
		pl->hetero = true;	/* cannot evaluate: keep it out of every domain answer */
		kfree(chain);
		return;
	}
	if (k > n) {
		dom = moobl_lower_inode(d_inode(chain[k - n]));
		pl->domain = dom->i_ino;
		for (i = k - n - 1; i >= 1 && ok; i--)
			ok = reach_ok(moobl_lower_inode(d_inode(chain[i])), dom, false);
		if (ok)
			ok = target_ok(moobl_lower_inode(self ? self : d_inode(chain[0])), dom);
		pl->hetero = !ok;
	}
	chain_put(chain, k);
	kfree(chain);
}

/* Depth of a layer dentry and the lower ino of its ancestor at depth n (0 when none or negative). */
static bool depth_and_domain(struct moobl_index *ix, struct dentry *dentry, unsigned int *depth,
			     u64 *domain)
{
	struct dentry **chain = kmalloc_array(MOOBL_MAX_DEPTH, sizeof(*chain), GFP_KERNEL);
	unsigned int n = ix->domain_depth, k;

	if (!chain || !walk_up(dentry, chain, &k)) {
		kfree(chain);
		return false;
	}
	*depth = k;
	*domain = 0;
	/* the dentry itself may be negative (a rename target): it then has no domain inode yet */
	if (n && k >= n && d_really_is_positive(chain[k - n]))
		*domain = moobl_lower_inode(d_inode(chain[k - n]))->i_ino;
	chain_put(chain, k);
	kfree(chain);
	return true;
}

/* ---- mount-time scan --------------------------------------------------- */

struct scan_name {
	struct list_head list;
	int len;
	char name[];
};

struct scan_ctx {
	struct dir_context ctx;
	struct list_head names;
	int err;
};

static bool scan_actor(struct dir_context *ctx, const char *name, int len, loff_t pos,
		       u64 ino, unsigned int d_type)
{
	struct scan_ctx *sc = container_of(ctx, struct scan_ctx, ctx);
	struct scan_name *sn;

	if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
		return true;
	sn = kmalloc(sizeof(*sn) + len, GFP_KERNEL);
	if (!sn) {
		sc->err = -ENOMEM;
		return false;
	}
	sn->len = len;
	memcpy(sn->name, name, len);
	list_add_tail(&sn->list, &sc->names);
	return true;
}

struct scan_dir {
	struct list_head list;
	struct dentry *dentry;		/* lower directory, referenced */
	char *path;			/* mount-relative, "" for the root */
	unsigned int depth;		/* 0 for the root */
	struct moobl_place self;	/* partition of the directory's own attributes */
	u64 inner;			/* domain of entries inside it; 0: none */
	bool inner_ok;			/* every directory from the domain down to it passes */
	struct dentry *dom;		/* lower domain directory, referenced, or NULL */
};

/* Reads the typed attributes of one lower dentry into the index. */
static int scan_attrs(struct super_block *sb, struct dentry *lower, u64 ino, const char *path,
		      const struct moobl_place *pl, char *list, size_t list_cap, u8 *value)
{
	struct moobl_index *ix = IX(sb);
	unsigned int nofs;
	ssize_t len;
	char *p;
	int err = 0;

	len = vfs_listxattr(lower, list, list_cap);
	if (len == -EOPNOTSUPP || len == -ENODATA || len == 0)
		return 0;
	if (len == -ERANGE)
		return 0;	/* more than list_cap bytes of names: left out of the index, reported by moobfs-check */
	if (len < 0)
		return (int)len;
	for (p = list; p < list + len && !err; p += strlen(p) + 1) {
		const char *name = p + MOOB_ATTR_PREFIX_LEN;
		ssize_t n;

		if (strncmp(p, MOOB_ATTR_PREFIX, MOOB_ATTR_PREFIX_LEN))
			continue;
		if (moob_attr_name_reserved(name, strlen(name)) || !moob_attr_name_ok(name, strlen(name)))
			continue;
		n = vfs_getxattr(&nop_mnt_idmap, lower, p, value, MOOB_ATTR_VALUE_MAX + 1);
		if (n <= 0 || moob_attr_validate(value, n) != MOOB_ATTR_OK)
			continue;	/* invalid encodings are not indexed; moobfs-check reports them */
		/*
		 * The lock is taken per attribute, never across the calls into the lower
		 * filesystem around it: those take inode locks under which the lower allocates,
		 * and reclaim takes this lock.
		 */
		nofs = ix_write_lock(ix);
		err = index_put(ix, ino, pl, name, value, n);
		if (!err) {
			struct moobl_ix_inode *rec = xa_load(&ix->inodes, ino);

			if (rec)
				index_set_path(ix, rec, path);
		}
		ix_write_unlock(ix, nofs);
	}
	return err;
}

/* A child directory of the scan: its record goes on the stack and takes cpath. */
static int scan_push_dir(struct moobl_index *ix, struct scan_dir *cur, struct dentry *child,
			 char *cpath, struct list_head *stack)
{
	struct scan_dir *nd = kzalloc(sizeof(*nd), GFP_KERNEL);
	unsigned int dn = ix->domain_depth;

	if (!nd) {
		kfree(cpath);
		return -ENOMEM;
	}
	nd->dentry = dget(child);
	nd->path = cpath;
	nd->depth = cur->depth + 1;
	if (dn && nd->depth == dn) {
		nd->inner = d_inode(child)->i_ino;
		nd->inner_ok = true;
		nd->dom = dget(child);
	} else if (dn && nd->depth > dn) {
		struct inode *dom = d_inode(cur->dom);

		nd->dom = dget(cur->dom);
		nd->inner = cur->inner;
		nd->self.domain = cur->inner;
		nd->self.hetero = !(cur->inner_ok && target_ok(d_inode(child), dom));
		nd->inner_ok = cur->inner_ok && reach_ok(d_inode(child), dom, false);
	}
	list_add(&nd->list, stack);
	return 0;
}

/* A child regular file of the scan: its attributes go into the index; frees cpath. */
static int scan_file(struct super_block *sb, struct scan_dir *cur, struct dentry *child,
		     char *cpath, char *list, u8 *value)
{
	struct moobl_place fpl = { .domain = cur->inner };
	int err;

	if (cur->inner)
		fpl.hetero = !(cur->inner_ok && target_ok(d_inode(child), d_inode(cur->dom)));
	err = scan_attrs(sb, child, d_inode(child)->i_ino, cpath, &fpl, list, XATTR_LIST_MAX,
			 value);
	kfree(cpath);
	return err;
}

/* One name read from a scanned directory: a directory is queued, a regular file indexed. */
static int scan_child(struct super_block *sb, struct scan_dir *cur, struct scan_name *sn,
		      struct list_head *stack, char *list, u8 *value)
{
	struct dentry *child = lookup_one_len_unlocked(sn->name, cur->dentry, sn->len);
	char *cpath;
	int err = 0;

	if (IS_ERR(child))
		return PTR_ERR(child);
	if (d_really_is_negative(child) || !(d_is_dir(child) || d_is_reg(child)))
		goto out;
	if (moobl_nested_subvolume(child)) {
		pr_info_once("moobfs_layer: a btrfs subvolume inside the lower tree is left out\n");
		goto out;
	}
	cpath = kasprintf(GFP_KERNEL, "%s%s%.*s", cur->path, cur->path[0] ? "/" : "", sn->len,
			  sn->name);
	if (!cpath) {
		err = -ENOMEM;
		goto out;
	}
	if (d_is_dir(child))
		err = scan_push_dir(IX(sb), cur, child, cpath, stack);
	else
		err = scan_file(sb, cur, child, cpath, list, value);
out:
	dput(child);
	return err;
}

int moobl_index_build(struct super_block *sb)
{
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	const struct cred *old;
	LIST_HEAD(stack);
	struct scan_dir *sd;
	char *list;
	u8 *value;
	int err = 0;

	if (!ix->enabled)
		return 0;
	list = kvmalloc(XATTR_LIST_MAX, GFP_KERNEL);
	value = kmalloc(MOOB_ATTR_VALUE_MAX + 1, GFP_KERNEL);
	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!list || !value || !sd) {
		err = -ENOMEM;
		goto out;
	}
	sd->dentry = dget(sbi->lower.dentry);
	sd->path = kstrdup("", GFP_KERNEL);
	if (!sd->path) {
		dput(sd->dentry);
		err = -ENOMEM;
		goto out;
	}
	list_add(&sd->list, &stack);
	sd = NULL;
	old = override_creds(sbi->creds);
	{
		ssize_t probe = vfs_listxattr(sbi->lower.dentry, NULL, 0);

		ix->xattr_ok = probe >= 0 || probe == -ENODATA;
	}
	while (!list_empty(&stack) && !err) {
		struct scan_dir *cur = list_first_entry(&stack, struct scan_dir, list);
		struct path dpath = { .mnt = sbi->lower.mnt, .dentry = cur->dentry };
		struct scan_ctx sc = { .ctx.actor = scan_actor, .names = LIST_HEAD_INIT(sc.names) };
		struct scan_name *sn, *tmp;
		struct file *dir;

		list_del(&cur->list);
		err = scan_attrs(sb, cur->dentry, d_inode(cur->dentry)->i_ino, cur->path, &cur->self,
				 list, XATTR_LIST_MAX, value);
		if (err)
			goto drop;
		dir = dentry_open(&dpath, O_RDONLY | O_DIRECTORY, sbi->creds);
		if (IS_ERR(dir)) {
			err = PTR_ERR(dir);
			goto drop;
		}
		err = iterate_dir(dir, &sc.ctx);
		fput(dir);
		if (!err)
			err = sc.err;
		list_for_each_entry_safe(sn, tmp, &sc.names, list) {
			if (!err)
				err = scan_child(sb, cur, sn, &stack, list, value);
			list_del(&sn->list);
			kfree(sn);
		}
drop:
		dput(cur->dentry);
		dput(cur->dom);
		kfree(cur->path);
		kfree(cur);
	}
	while (!list_empty(&stack)) {
		struct scan_dir *left = list_first_entry(&stack, struct scan_dir, list);

		list_del(&left->list);
		dput(left->dentry);
		dput(left->dom);
		kfree(left->path);
		kfree(left);
	}
	revert_creds(old);
	if (err)
		pr_warn("moobfs_layer: index build failed (%d); mount with noindex to skip it\n", err);
out:
	kfree(sd);
	kfree(value);
	kvfree(list);
	return err;
}

void moobl_index_init(struct super_block *sb, u64 budget, bool enabled, unsigned int domain_depth)
{
	struct moobl_index *ix = IX(sb);

	/* a cursor carries the value of the entry it stopped at */
	BUILD_BUG_ON(MOOB_ATTR_STRING_MAX > MOOB_QUERY_VALUE_MAX);
	init_rwsem(&ix->lock);
	/* teach lockdep that reclaim takes this lock, so it reports an allocation outside a NOFS scope */
	fs_reclaim_acquire(GFP_KERNEL);
	might_lock(&ix->lock);
	fs_reclaim_release(GFP_KERNEL);
	ix->names = RB_ROOT;
	xa_init(&ix->inodes);
	xa_init(&ix->fallback);
	ix->domain_depth = domain_depth;
	get_random_bytes(&ix->cursor_key, sizeof(ix->cursor_key));
	ix->budget = budget;
	ix->enabled = enabled;
	ix->initialized = true;
}

void moobl_index_destroy(struct super_block *sb)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	unsigned long ino;

	if (!ix->initialized)
		return;	/* fill_super failed before the index existed */
	nofs = ix_write_lock(ix);
	xa_for_each(&ix->inodes, ino, rec) {
		struct moobl_ix_entry *e, *tmp;

		list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	xa_destroy(&ix->inodes);
	xa_destroy(&ix->fallback);
	ix_write_unlock(ix, nofs);
}

/* ---- updates from main.c ------------------------------------------------ */

static void record_dentry_path(struct moobl_index *ix, struct moobl_ix_inode *rec,
			       struct dentry *dentry)
{
	char *buf = (char *)__get_free_page(GFP_KERNEL);
	char *p;

	lockdep_assert_held_write(&ix->lock);
	if (!buf)
		return;
	p = dentry_path_raw(dentry, buf, PAGE_SIZE);
	if (!IS_ERR(p))
		index_set_path(ix, rec, *p == '/' ? p + 1 : p);
	free_page((unsigned long)buf);
}

int moobl_index_set(struct super_block *sb, struct dentry *dentry, const char *name,
		    const void *value, size_t size)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct inode *inode = d_inode(dentry);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;
	int err;

	if (!ix->enabled)
		return 0;
	if (((const u8 *)value)[0] == MOOB_T_RAW) {
		/* raw is stored and never indexed: an indexed value it replaces must go */
		moobl_index_remove(sb, inode->i_ino, name);
		return 0;
	}
	place_dentry(ix, dentry, NULL, &pl);	/* may sleep: outside the index lock */
	nofs = ix_write_lock(ix);
	err = index_put(ix, inode->i_ino, &pl, name, value, size);
	if (err) {
		/* The lower write already succeeded; the index is now incomplete until remount. */
		if (!ix->incomplete)
			pr_warn("moobfs_layer: index update failed (%d); QUERY results are incomplete until remount\n", err);
		ix->incomplete = true;
	} else {
		rec = xa_load(&ix->inodes, inode->i_ino);
		if (rec) {
			rec->upper = inode;
			record_dentry_path(ix, rec, dentry);
		}
	}
	ix_write_unlock(ix, nofs);
	return err;
}

void moobl_index_remove(struct super_block *sb, u64 ino, const char *name)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_ix_entry *e;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, ino);
	if (rec) {
		e = entry_find(rec, name);
		if (e)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	ix_write_unlock(ix, nofs);
}

void moobl_index_drop_inode(struct super_block *sb, u64 ino)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_ix_entry *e, *tmp;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, ino);
	if (rec) {
		list_for_each_entry_safe(e, tmp, &rec->entries, by_ino)
			entry_free(ix, e);
		inode_rec_put_if_empty(ix, rec);
	}
	ix_write_unlock(ix, nofs);
}

/*
 * inode now lives at the name of where (the rename target dentry, called before the VFS
 * moves the dentries, so where already carries the new parent and name).
 */
void moobl_index_renamed(struct super_block *sb, struct inode *inode, struct dentry *where)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;

	if (!ix->enabled)
		return;
	if (S_ISDIR(inode->i_mode)) {
		/* Same depth and domain (EXDEV otherwise): only the recorded names went stale. */
		nofs = ix_write_lock(ix);
		ix->paths_stale = true;	/* descendants keep old names; QUERY uses the dentry cache */
		ix_write_unlock(ix, nofs);
		return;
	}
	place_dentry(ix, where, inode, &pl);	/* a file may change domain: move its entries */
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (rec) {
		record_dentry_path(ix, rec, where);
		rec_set_partition(ix, rec, &pl);
	}
	ix_write_unlock(ix, nofs);
}

/*
 * A change of mode, owner, group, access ACL or link count. For a file or a directory
 * carrying attributes, its own partition is recomputed. For a directory inside a domain
 * (or the domain directory itself), every file below it may change homogeneity: rather
 * than walking the subtree, the domain falls back to per-result checks until the next
 * mount (docs/spec-query-v2.md §3; recorded in journal 0035).
 */
void moobl_index_perm_changed(struct super_block *sb, struct dentry *dentry)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;
	struct moobl_place pl;
	unsigned int depth;
	u64 domain;

	if (!ix->enabled || !ix->domain_depth || d_really_is_negative(dentry))
		return;
	if (d_is_dir(dentry) && depth_and_domain(ix, dentry, &depth, &domain) &&
	    depth >= ix->domain_depth && domain)
		if (xa_err(xa_store(&ix->fallback, (unsigned long)domain, xa_mk_value(1),
				    GFP_KERNEL)))
			WRITE_ONCE(ix->fallback_all, true);	/* no memory to note it: check everywhere */
	place_dentry(ix, dentry, NULL, &pl);
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, d_inode(dentry)->i_ino);
	if (rec)
		rec_set_partition(ix, rec, &pl);
	ix_write_unlock(ix, nofs);
}

/*
 * A directory keeps its depth and domain across a rename, otherwise the partition of its
 * whole subtree would change: the caller returns EXDEV (docs/spec-query-v2.md §5).
 */
bool moobl_rename_crosses_domain(struct super_block *sb, struct dentry *old_dentry,
				 struct dentry *new_dentry)
{
	struct moobl_index *ix = IX(sb);
	unsigned int od, nd;
	u64 odom, ndom;

	if (!ix->enabled || !ix->domain_depth)
		return false;
	if (!depth_and_domain(ix, old_dentry, &od, &odom) ||
	    !depth_and_domain(ix, new_dentry, &nd, &ndom))
		return true;
	if (od == ix->domain_depth)
		odom = ndom = 0;	/* a domain directory itself: its domain is its own inode */
	return od != nd || odom != ndom;
}

void moobl_index_inode_cached(struct super_block *sb, struct inode *inode)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (rec)
		rec->upper = inode;
	ix_write_unlock(ix, nofs);
}

void moobl_index_inode_evicted(struct super_block *sb, struct inode *inode)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moobl_ix_inode *rec;

	if (!ix->enabled)
		return;
	nofs = ix_write_lock(ix);
	rec = xa_load(&ix->inodes, inode->i_ino);
	if (rec && rec->upper == inode)
		rec->upper = NULL;
	ix_write_unlock(ix, nofs);
}

/* ---- QUERY -------------------------------------------------------------- */

/* allowed: the term flags the caller's request version accepts (0 for the v1 QUERY). */
static int term_check(const struct moob_query_term *t, u8 allowed)
{
	size_t nlen = strnlen(t->name, sizeof(t->name));

	if (nlen == sizeof(t->name) || !moob_attr_name_ok(t->name, nlen) || (t->flags & ~allowed) ||
	    t->pad[0] || t->pad[1] || t->pad[2])
		return -EINVAL;
	if (t->op == MOOB_OP_EXISTS)	/* "exists or missing" would match everything */
		return (t->type || t->len || t->flags) ? -EINVAL : 0;
	if (t->op < MOOB_OP_EQ || t->op > MOOB_OP_PREFIX)
		return -EINVAL;
	switch (t->type) {
	case MOOB_T_STRING:
		if (t->len > MOOB_QUERY_VALUE_MAX || !moob_utf8_valid(t->value, t->len))
			return -EINVAL;
		return 0;
	case MOOB_T_INT64:
	case MOOB_T_TIME:
	case MOOB_T_DOUBLE:
		if (t->len != 8 || t->op == MOOB_OP_PREFIX)
			return -EINVAL;
		if (t->type == MOOB_T_DOUBLE) {
			u8 v[9] = { MOOB_T_DOUBLE };

			memcpy(v + 1, t->value, 8);
			if (moob_attr_validate(v, 9) != MOOB_ATTR_OK)
				return -EINVAL;
		}
		return 0;
	case MOOB_T_BOOL:
		if (t->len != 1 || t->value[0] > 1 || t->op == MOOB_OP_PREFIX)
			return -EINVAL;
		return 0;
	default:
		return -EINVAL;	/* raw values are not indexed and cannot be queried */
	}
}

static bool entry_matches(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	int c;

	if (t->op == MOOB_OP_EXISTS)
		return true;
	if (e->type != t->type)
		return false;
	if (t->op == MOOB_OP_PREFIX)
		return e->len >= t->len && !memcmp(e->value, t->value, t->len);
	c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
	switch (t->op) {
	case MOOB_OP_EQ: return c == 0;
	case MOOB_OP_LT: return c < 0;
	case MOOB_OP_LE: return c <= 0;
	case MOOB_OP_GT: return c > 0;
	case MOOB_OP_GE: return c >= 0;
	default: return false;
	}
}

static bool inode_matches_rest(struct moobl_ix_inode *rec, const struct moob_query_term *terms,
			       u32 nterms, struct moobl_ix_name *const *tn)
{
	u32 i;

	for (i = 1; i < nterms; i++) {
		struct moobl_ix_entry *e = tn ? entry_find_node(rec, tn[i]) :
					   entry_find(rec, terms[i].name);

		if (!e) {
			if (!(terms[i].flags & MOOB_TERM_MISSING_OK))
				return false;
			continue;
		}
		if (!entry_matches(e, &terms[i]))
			return false;
	}
	return true;
}

/* Ordered scans walk the order attribute: every term is tested on the inode. */
static bool inode_matches_all(struct moobl_ix_inode *rec, const struct moob_query_term *terms,
			      u32 nterms, struct moobl_ix_name *const *tn)
{
	u32 i;

	for (i = 0; i < nterms; i++) {
		struct moobl_ix_entry *e = tn ? entry_find_node(rec, tn[i]) :
					   entry_find(rec, terms[i].name);

		if (!e) {
			if (!(terms[i].flags & MOOB_TERM_MISSING_OK))
				return false;
			continue;
		}
		if (!entry_matches(e, &terms[i]))
			return false;
	}
	return true;
}

/*
 * The name of rec, relative to the mount root: from the dentry cache when the inode is in
 * memory, else the name recorded at the scan. NULL when neither is known. *held gets a dentry
 * reference to drop once the index lock is released: dropping the last reference to an
 * unlinked file evicts its inode, and eviction takes the index lock.
 */
static const char *result_path(struct moobl_index *ix, struct moobl_ix_inode *rec,
			       char *pathbuf, struct dentry **held)
{
	lockdep_assert_held(&ix->lock);
	*held = NULL;
	if (rec->upper) {
		struct dentry *alias = d_find_alias(rec->upper);

		if (alias) {
			char *p = dentry_path_raw(alias, pathbuf, PAGE_SIZE);

			*held = alias;
			if (!IS_ERR(p))
				return *p == '/' ? p + 1 : p;
		}
	}
	if (rec->path && !ix->paths_stale)
		return rec->path;
	return NULL;
}

/*
 * Candidates are gathered under the index lock in bounded chunks, then checked
 * against the caller's own credentials with the lock released: the recorded
 * path is resolved through the VFS (search permission on every directory,
 * read permission on the file), the resolved object must belong to this mount
 * and carry the indexed inode number. Entries the caller may not reach are
 * omitted silently and do not count toward limit or truncated, so neither
 * existence nor count leaks. Operational failures (ENOMEM, EFAULT) are
 * returned, never turned into an empty answer. The scan resumes after each
 * chunk from the last (type, value, ino) key, which stays meaningful even if
 * the tree changed meanwhile, so the number of examined candidates is not
 * bounded by the result limit.
 */
#define MOOBL_QUERY_CHUNK 256U

struct moobl_candidate {
	u64 ino;
	char *path;	/* mount-relative, or NULL when unknown */
	struct dentry *held;	/* dropped after the index lock, see result_path() */
	u8 type;	/* scan key of the entry that produced it (QUERY2 cursors) */
	u16 len;
	u8 *value;
};

struct moobl_resume {
	u64 pdomain;		/* partition being scanned */
	bool phetero;
	bool valid;		/* the entry key below is set */
	u8 type;
	u16 len;
	u64 ino;
	int lead;		/* equality plan: index into leap_idx of the scanned run; -1: unset */
	u8 value[MOOB_QUERY_VALUE_MAX];
};

static bool caller_may_read(struct file *file, struct super_block *sb, const char *path, u64 ino)
{
	struct path p;
	bool ok = false;

	if (!path || !*path)
		return false;
	if (vfs_path_lookup(file->f_path.dentry, file->f_path.mnt, path, 0, &p))
		return false;
	if (p.mnt == file->f_path.mnt && p.dentry->d_sb == sb && d_inode(p.dentry) &&
	    d_inode(p.dentry)->i_ino == ino &&
	    !inode_permission(&nop_mnt_idmap, d_inode(p.dentry), MAY_READ))
		ok = true;
	path_put(&p);
	return ok;
}

/* First entry strictly after the resume key, or the first entry when no key. */
static struct rb_node *first_after(struct moobl_ix_name *nm, const struct moobl_resume *r)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	if (!r->valid)
		return rb_first(&nm->entries);
	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, r->type, r->value, r->len, r->ino) > 0) {
			best = n;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best;
}

/* Last entry strictly before the resume key (descending scans). */
static struct rb_node *last_before(struct moobl_ix_name *nm, const struct moobl_resume *r)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, r->type, r->value, r->len, r->ino) < 0) {
			best = n;
			n = n->rb_right;
		} else {
			n = n->rb_left;
		}
	}
	return best;
}

/* Ordered scans: the first (ascending) or last (descending) entry of the order type. */
static struct rb_node *seek_type(struct moobl_ix_name *nm, u8 type, bool desc)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);
		bool go_left = desc ? e->type > type : e->type >= type;

		if (go_left) {
			if (!desc)
				best = n;
			n = n->rb_left;
		} else {
			if (desc)
				best = n;
			n = n->rb_right;
		}
	}
	return best;
}

/*
 * Where the scan of the first term starts and where it can stop. Entries are ordered by
 * (type, value, ino), so every op except EXISTS matches a contiguous run: EQ, GE, GT and
 * PREFIX start at the first entry whose (type, value) is not below the term; LT and LE start
 * at the first entry of the term's type. EXISTS keeps the full scan.
 */
static struct rb_node *seek_first(struct moobl_ix_name *nm, const struct moob_query_term *t)
{
	struct rb_node *n = nm->entries.rb_node, *best = NULL;

	if (t->op == MOOB_OP_EXISTS)
		return rb_first(&nm->entries);
	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);
		int c;

		if (t->op == MOOB_OP_LT || t->op == MOOB_OP_LE)
			c = e->type < t->type ? -1 : 0;
		else
			c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
		if (c >= 0) {
			best = n;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	return best;
}

/* True when no entry at or after e (in tree order) can match the term. */
static bool past_end(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	int c;

	if (t->op == MOOB_OP_EXISTS)
		return false;
	if (e->type != t->type)
		return e->type > t->type;
	c = value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
	switch (t->op) {
	case MOOB_OP_EQ:
	case MOOB_OP_LE:
		return c > 0;
	case MOOB_OP_LT:
		return c >= 0;
	case MOOB_OP_PREFIX:
		/* strings with a given prefix form one run starting at the prefix itself */
		return c > 0 && !(e->len >= t->len && !memcmp(e->value, t->value, t->len));
	default:
		return false;
	}
}

/* What a query scans and how its results are authorized and named. */
struct moobl_qscope {
	bool all;		/* v1: every partition of terms[0].name */
	u64 domain;		/* QUERY2: the domain's homogeneous partition */
	bool check;		/* authorize each result with the caller's credentials */
	unsigned int strip;	/* leading path components dropped from mount-relative paths */
	const char *scan;	/* attribute tree walked: order_by, or terms[0].name */
	bool ordered;		/* walk the order_type run of scan; test every term */
	bool desc;
	u8 otype;
	bool leap;		/* intersect the equality terms below by leapfrog (spec §4.4) */
	u8 nleap;
	u8 leap_idx[MOOB_QUERY_MAX_TERMS];	/* equality terms; the first one leads */
};

/* Path relative to the scope: the mount-relative path without its first `strip` names. */
static const char *scope_path(const char *path, unsigned int strip)
{
	while (strip--) {
		path = strchr(path, '/');
		if (!path)
			return NULL;
		path++;
	}
	return *path ? path : NULL;
}

/* Records the inode of e as a candidate; with want_key, also e's scan key for the cursor. */
static int add_candidate(struct moobl_index *ix, struct moobl_ix_entry *e,
			 const struct moobl_qscope *sc, struct moobl_candidate *c, char *pathbuf,
			 bool want_key)
{
	const char *path;
	int err = 0;

	lockdep_assert_held(&ix->lock);
	path = result_path(ix, e->owner, pathbuf, &c->held);
	c->ino = e->owner->ino;
	c->path = NULL;
	c->value = NULL;
	if (path) {
		const char *rel = scope_path(path, sc->strip);

		/* the whole name: the permission check resolves it; only the reply is cut */
		if (rel) {
			c->path = kstrdup(rel, GFP_KERNEL);
			if (!c->path)
				err = -ENOMEM;
		}
	}
	if (want_key) {
		c->type = e->type;
		c->len = e->len;
		c->value = kmalloc(max_t(u16, e->len, 1), GFP_KERNEL);
		if (c->value)
			memcpy(c->value, e->value, e->len);
		else
			err = -ENOMEM;
	}
	return err;
}

/* First entry of the equality run of t whose inode is >= ino, or NULL past the run. */
static struct moobl_ix_entry *seek_eq(struct moobl_ix_name *nm, const struct moob_query_term *t,
				      u64 ino)
{
	struct rb_node *n = nm->entries.rb_node;
	struct moobl_ix_entry *best = NULL;

	while (n) {
		struct moobl_ix_entry *e = rb_entry(n, struct moobl_ix_entry, node);

		if (entry_cmp(e, t->type, t->value, t->len, ino) >= 0) {
			best = e;
			n = n->rb_left;
		} else {
			n = n->rb_right;
		}
	}
	if (!best || best->type != t->type ||
	    value_cmp(best->type, best->value, best->len, t->type, t->value, t->len))
		return NULL;
	return best;
}

/* True while e is still inside the equality run of t. */
static bool in_eq_run(const struct moobl_ix_entry *e, const struct moob_query_term *t)
{
	return e->type == t->type &&
	       !value_cmp(e->type, e->value, e->len, t->type, t->value, t->len);
}

/*
 * Equality plan (docs/spec-query-v2.md §4.4, revised by journal 0044): inside one value, the
 * entries of an equality term are ordered by inode. Once per call, the runs of all equality
 * terms are walked in lockstep from the resume inode until the first one ends: that one is
 * the shortest, at a cost proportional to it. Then that run alone is scanned and every term is
 * tested on each inode. Results come in inode order whichever run leads, so a call resumes
 * after the last inode only. The first implementation (journal 0037) was a leapfrog with one
 * tree seek per step; on the benchmark host it was 2.7 to 3.6 times slower than scanning a
 * short run, and only faster when the first term's run was long. Called with the index lock
 * held for reading.
 */
static int leap_collect(struct moobl_index *ix, const struct moobl_qscope *sc,
			const struct moob_query_term *terms, u32 nterms,
			struct moobl_resume *resume, struct moobl_candidate *cand, u32 *ncand,
			u32 cap, char *pathbuf, bool want_key, bool *more)
{
	struct moobl_ix_name *nm[MOOB_QUERY_MAX_TERMS];
	struct moobl_ix_name *tn[MOOB_QUERY_MAX_TERMS];
	struct rb_node *it[MOOB_QUERY_MAX_TERMS];
	const struct moob_query_term *lt;
	struct moobl_ix_entry *e;
	u64 x = 0;
	u32 k;

	lockdep_assert_held(&ix->lock);
	for (k = 0; k < nterms; k++)
		tn[k] = name_find(ix, terms[k].name, sc->domain, false);

	if (resume->valid) {
		if (resume->ino == U64_MAX)
			return 0;
		x = resume->ino + 1;
	}
	for (k = 0; k < sc->nleap; k++) {
		nm[k] = name_find(ix, terms[sc->leap_idx[k]].name, sc->domain, false);
		if (!nm[k])
			return 0;
	}
	if (resume->lead < 0) {
		for (k = 0; k < sc->nleap; k++) {
			e = seek_eq(nm[k], &terms[sc->leap_idx[k]], x);
			if (!e)
				return 0;	/* an empty run: the intersection is empty */
			it[k] = &e->node;
		}
		while (resume->lead < 0) {
			for (k = 0; k < sc->nleap; k++) {
				struct rb_node *n = rb_next(it[k]);

				if (!n || !in_eq_run(rb_entry(n, struct moobl_ix_entry, node),
						     &terms[sc->leap_idx[k]])) {
					resume->lead = (int)k;	/* the first run to end */
					break;
				}
				it[k] = n;
			}
		}
	}
	lt = &terms[sc->leap_idx[resume->lead]];
	for (e = seek_eq(nm[resume->lead], lt, x); e;) {
		struct rb_node *n;

		resume->valid = true;
		resume->type = e->type;
		resume->len = e->len;
		resume->ino = e->owner->ino;
		memcpy(resume->value, e->value, e->len);
		if (inode_matches_all(e->owner, terms, nterms, tn)) {
			int err = add_candidate(ix, e, sc, &cand[*ncand], pathbuf, want_key);

			(*ncand)++;
			if (err)
				return err;
			if (*ncand == cap) {
				*more = true;
				return 0;
			}
		}
		n = rb_next(&e->node);
		e = n ? rb_entry(n, struct moobl_ix_entry, node) : NULL;
		if (e && !in_eq_run(e, lt))
			e = NULL;
	}
	return 0;
}

/* Copies one candidate to the caller as a result; last gets its scan key (cursor). */
static int put_result(struct moob_query_result __user *dst, struct moob_query_result *one,
		      const struct moobl_candidate *c, struct moobl_resume *last)
{
	memset(one, 0, sizeof(*one));
	one->ino = c->ino;
	if (c->path) {
		size_t len = strlen(c->path);

		if (len >= MOOB_QUERY_PATH_MAX) {
			one->flags = MOOB_RES_PATH_TRUNCATED;
			len = MOOB_QUERY_PATH_MAX - 1;
		}
		one->path_len = (u32)len;
		memcpy(one->path, c->path, len);	/* the record is zeroed: NUL follows */
	} else {
		one->flags = MOOB_RES_PATH_UNKNOWN;
	}
	if (copy_to_user(dst, one, sizeof(*one)))
		return -EFAULT;
	if (last) {
		last->valid = true;
		last->type = c->type;
		last->len = c->len;
		last->ino = c->ino;
		memcpy(last->value, c->value, c->len);
	}
	return 0;
}

/*
 * Runs one query. start (may be NULL) resumes strictly after (or, descending, before) a
 * scan key; last (may be NULL) receives the key of the last result returned.
 */
static long query_run(struct super_block *sb, struct file *file,
		      const struct moob_query_term *terms, u32 nterms, u32 limit, u64 results,
		      const struct moobl_qscope *sc, const struct moobl_resume *start,
		      struct moobl_resume *last, u32 *count_out, u32 *truncated_out)
{
	unsigned int nofs;
	struct moobl_index *ix = IX(sb);
	struct moob_query_result __user *uout = u64_to_user_ptr(results);
	struct moob_query_result *one;
	struct moobl_candidate *cand;
	struct moobl_resume *resume;
	char *pathbuf;
	struct moobl_ix_name *tn_buf[MOOB_QUERY_MAX_TERMS];
	struct moobl_ix_name **tn = sc->all ? NULL : tn_buf;
	u32 i, count = 0, truncated = 0;
	bool exhausted = false;
	long err = 0;

	/*
	 * one result at a time is copied to the caller's array, so the kernel never allocates
	 * (or zeroes) a limit-sized buffer: the cost is per result, not per requested limit
	 */
	one = kmalloc(sizeof(*one), GFP_KERNEL);
	cand = kcalloc(MOOBL_QUERY_CHUNK, sizeof(*cand), GFP_KERNEL);
	resume = kzalloc(sizeof(*resume), GFP_KERNEL);
	pathbuf = (char *)__get_free_page(GFP_KERNEL);
	if (!one || !cand || !resume || !pathbuf) {
		err = -ENOMEM;
		goto out;
	}
	if (start)
		*resume = *start;
	resume->lead = -1;
	resume->pdomain = sc->all ? resume->pdomain : sc->domain;
	if (last)
		last->valid = false;
	while (!exhausted && !err) {
		struct moobl_ix_name *nm;
		struct moobl_ix_entry *seen = NULL;
		struct rb_node *n;
		u32 ncand = 0;
		bool more = false;
		/*
		 * Without per-result checks every candidate is a result, so a chunk needs at most
		 * the results still missing plus one (that one only sets truncated). Gathering a
		 * full chunk for a small limit walked far past the answer (journal 0048).
		 */
		u32 cap = sc->check ? MOOBL_QUERY_CHUNK :
			  min_t(u32, MOOBL_QUERY_CHUNK, limit - count + 1);

		nofs = ix_read_lock(ix);
		if (sc->leap) {
			err = leap_collect(ix, sc, terms, nterms, resume, cand, &ncand, cap, pathbuf,
					   last != NULL, &more);
			goto chunk_done;
		}
		/*
		 * QUERY2: candidates all sit in the domain's homogeneous partition, so each term's
		 * attribute is one name node and entries are matched by pointer, not by strcmp
		 */
		if (!sc->all)
			for (i = 0; i < nterms; i++)
				tn[i] = name_find(ix, terms[i].name, sc->domain, false);
		nm = sc->all ? name_lower_bound(ix, sc->scan, resume->pdomain, resume->phetero)
			     : name_find(ix, sc->scan, sc->domain, false);
		if (nm && (nm->domain != resume->pdomain || nm->hetero != resume->phetero)) {
			resume->pdomain = nm->domain;	/* the partition we stopped in is gone */
			resume->phetero = nm->hetero;
			resume->valid = false;
		}
		if (!nm)
			n = NULL;
		else if (resume->valid)
			n = sc->desc ? last_before(nm, resume) : first_after(nm, resume);
		else
			n = sc->ordered ? seek_type(nm, sc->otype, sc->desc) : seek_first(nm, &terms[0]);
		while (nm && !err) {
			struct moobl_ix_entry *e = n ? rb_entry(n, struct moobl_ix_entry, node) : NULL;
			bool end = !e || (sc->ordered ? e->type != sc->otype : past_end(e, &terms[0]));
			bool match;

			if (end) {
				struct rb_node *next = sc->all ? rb_next(&nm->node) : NULL;
				struct moobl_ix_name *nx = next ? rb_entry(next, struct moobl_ix_name, node)
								: NULL;

				if (!nx || strcmp(nx->name, sc->scan))
					break;
				nm = nx;	/* v1: next partition of the same name */
				resume->pdomain = nm->domain;
				resume->phetero = nm->hetero;
				resume->valid = false;
				seen = NULL;
				n = seek_first(nm, &terms[0]);
				continue;
			}
			seen = e;	/* the resume key is copied from it when the chunk ends */
			n = sc->desc ? rb_prev(n) : rb_next(n);
			if (sc->ordered)
				match = inode_matches_all(e->owner, terms, nterms, tn);
			else
				match = entry_matches(e, &terms[0]) &&
					inode_matches_rest(e->owner, terms, nterms, tn);
			if (!match)
				continue;
			err = add_candidate(ix, e, sc, &cand[ncand], pathbuf, last != NULL);
			ncand++;
			if (ncand == cap) {
				more = true;
				break;
			}
		}
		if (seen) {
			resume->valid = true;
			resume->type = seen->type;
			resume->len = seen->len;
			resume->ino = seen->owner->ino;
			memcpy(resume->value, seen->value, seen->len);
		}
chunk_done:
		if (!more)
			exhausted = true;
		ix_read_unlock(ix, nofs);
		for (i = 0; i < ncand; i++) {
			bool ok = !err && (!sc->check ||
					   caller_may_read(file, sb, cand[i].path, cand[i].ino));

			if (ok && count == limit) {
				truncated = 1;
				exhausted = true;
			} else if (ok) {
				err = put_result(&uout[count], one, &cand[i], last);
				if (!err)
					count++;
			}
			dput(cand[i].held);
			cand[i].held = NULL;
			kfree(cand[i].path);
			cand[i].path = NULL;
			kfree(cand[i].value);
			cand[i].value = NULL;
		}
	}
	*count_out = count;
	*truncated_out = truncated;
out:
	if (cand)
		for (i = 0; i < MOOBL_QUERY_CHUNK; i++) {
			dput(cand[i].held);
			kfree(cand[i].path);
			kfree(cand[i].value);
		}
	kfree(cand);
	kfree(resume);
	if (pathbuf)
		free_page((unsigned long)pathbuf);
	kfree(one);
	return err;
}

/* v1: every partition, each result checked; privileged when domains are on (spec §4.5). */
static long do_query(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moobl_index *ix = IX(sb);
	struct moobl_qscope sc = { .all = true, .check = true };
	struct moob_query *q;
	u32 i;
	long err = 0;

	if (!ix->enabled)
		return -EOPNOTSUPP;	/* noindex (ENOSYS until journal 0050) */
	if (ix->domain_depth && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	q = memdup_user(arg, sizeof(*q));
	if (IS_ERR(q))
		return PTR_ERR(q);
	if (q->version != MOOB_UAPI_VERSION || !q->nterms || q->nterms > MOOB_QUERY_MAX_TERMS ||
	    !q->limit || q->limit > MOOB_QUERY_MAX_LIMIT || q->flags || q->reserved[0] ||
	    q->reserved[1]) {
		err = -EINVAL;
		goto out;
	}
	for (i = 0; i < q->nterms && !err; i++)
		err = term_check(&q->terms[i], 0);
	sc.scan = q->terms[0].name;
	if (!err)
		err = query_run(sb, file, q->terms, q->nterms, q->limit, q->results, &sc, NULL, NULL,
				&q->count, &q->truncated);
	if (!err && copy_to_user(arg, q, sizeof(*q)))
		err = -EFAULT;
out:
	kfree(q);
	return err;
}

static bool all_zero(const void *p, size_t n)
{
	const u8 *b = p;

	while (n--)
		if (*b++)
			return false;
	return true;
}

/*
 * QUERY2 cursor (docs/spec-query-v2.md §4.1): the scan key of the last result returned,
 * a hash of the query (domain, fallback state, order and terms; not the limit) and a MAC,
 * both keyed with a per-mount random key, so a cursor from another mount or query, a
 * corrupted one, or one issued before the domain fell back is refused with ESTALE. Values
 * longer than the inline space are recovered from the inode's current entry and must
 * still agree with the stored prefix and length.
 */
#define MOOBL_CURSOR_MAGIC 0xc2
#define MOOBL_CURSOR_INLINE 96

struct moobl_cursor {
	u8 magic;
	u8 type;
	u16 len;
	u8 pad[4];
	u64 ino;
	u64 qhash;
	u8 value[MOOBL_CURSOR_INLINE];
	u64 mac;
} __packed;

struct moobl_qhash_head {
	u64 domain;
	u8 fallback;
	u8 order;
	u8 order_type;
	u8 pad;
	u32 nterms;
	char order_by[MOOB_QUERY_NAME_MAX + 1];
};

static int query_hash(struct moobl_index *ix, const struct moob_query2 *q, u64 domain,
		      bool fallback, u64 *out)
{
	size_t tsz = (size_t)q->nterms * sizeof(q->terms[0]);
	struct moobl_qhash_head *h = kzalloc(sizeof(*h) + tsz, GFP_KERNEL);

	if (!h)
		return -ENOMEM;
	h->domain = domain;
	h->fallback = fallback;
	h->order = q->order;
	h->order_type = q->order_type;
	h->nterms = q->nterms;
	memcpy(h->order_by, q->order_by, sizeof(h->order_by));
	memcpy(h + 1, q->terms, tsz);
	*out = siphash(h, sizeof(*h) + tsz, &ix->cursor_key);
	kfree(h);
	return 0;
}

static void cursor_encode(struct moobl_index *ix, u8 *out, const struct moobl_resume *r,
			  u64 qhash)
{
	struct moobl_cursor c;

	BUILD_BUG_ON(sizeof(c) != MOOB_QUERY2_CURSOR);
	memset(&c, 0, sizeof(c));
	c.magic = MOOBL_CURSOR_MAGIC;
	c.type = r->type;
	c.len = r->len;
	c.ino = r->ino;
	c.qhash = qhash;
	memcpy(c.value, r->value, min_t(u16, r->len, MOOBL_CURSOR_INLINE));
	c.mac = siphash(&c, offsetof(struct moobl_cursor, mac), &ix->cursor_key);
	memcpy(out, &c, sizeof(c));
}

static int cursor_decode(struct moobl_index *ix, const u8 *in, u64 qhash, const char *scan,
			 const struct moobl_qscope *sc, struct moobl_resume *r)
{
	unsigned int nofs;
	struct moobl_cursor c;

	memcpy(&c, in, sizeof(c));
	if (c.magic != MOOBL_CURSOR_MAGIC ||
	    c.mac != siphash(&c, offsetof(struct moobl_cursor, mac), &ix->cursor_key) ||
	    c.qhash != qhash || c.len > MOOB_QUERY_VALUE_MAX ||
	    (sc->ordered && c.type != sc->otype))
		return -ESTALE;
	r->valid = true;
	r->type = c.type;
	r->len = c.len;
	r->ino = c.ino;
	if (sc->leap) {
		r->len = 0;	/* the equality plan resumes after the inode, whichever run leads */
	} else if (c.len <= MOOBL_CURSOR_INLINE) {
		memcpy(r->value, c.value, c.len);
	} else {
		struct moobl_ix_inode *rec;
		struct moobl_ix_entry *e = NULL;
		int err = -ESTALE;

		nofs = ix_read_lock(ix);
		rec = xa_load(&ix->inodes, c.ino);
		if (rec)
			e = entry_find(rec, scan);
		if (e && e->type == c.type && e->len == c.len &&
		    !memcmp(e->value, c.value, MOOBL_CURSOR_INLINE)) {
			memcpy(r->value, e->value, e->len);
			err = 0;
		}
		ix_read_unlock(ix, nofs);
		if (err)
			return err;
	}
	return 0;
}

static int order_check(const struct moob_query2 *q)
{
	size_t n = strnlen(q->order_by, sizeof(q->order_by));

	if (!n)
		return (q->order || q->order_type || !q->nterms) ? -EINVAL : 0;
	if (n == sizeof(q->order_by) || !moob_attr_name_ok(q->order_by, n))
		return -EINVAL;
	if (q->order != MOOB_ORDER_ASC && q->order != MOOB_ORDER_DESC)
		return -EINVAL;
	switch (q->order_type) {
	case MOOB_T_STRING:
	case MOOB_T_INT64:
	case MOOB_T_DOUBLE:
	case MOOB_T_TIME:
		return 0;
	default:
		return -EINVAL;
	}
}

/*
 * QUERY2 on a domain directory (docs/spec-query-v2.md §4): the caller must be able to read
 * and traverse the domain directory now; results come from the domain's homogeneous
 * partition without per-result checks, unless a permission change inside the domain made
 * it fall back to them. With order_by the attribute's entries of order_type are walked in
 * the requested direction; a cursor resumes after the last result of a previous call.
 */
static long query2_target(struct super_block *sb, struct file *file, u64 *domain,
			  unsigned int *depth)
{
	struct moobl_index *ix = IX(sb);

	if (!ix->enabled || !ix->domain_depth)
		return -EOPNOTSUPP;	/* noindex (ENOSYS until journal 0050) or domains off */
	if (!d_is_dir(file->f_path.dentry) ||
	    !depth_and_domain(ix, file->f_path.dentry, depth, domain) ||
	    *depth != ix->domain_depth)
		return -ENOTTY;
	if (inode_permission(&nop_mnt_idmap, file_inode(file), MAY_EXEC | MAY_READ))
		return -EACCES;
	return 0;
}

static long query2_validate(const struct moob_query2 *q)
{
	u32 i, plain = 0;
	long err;

	if (q->version != MOOB_QUERY2_VERSION || q->nterms > MOOB_QUERY_MAX_TERMS ||
	    !q->limit || q->limit > MOOB_QUERY_MAX_LIMIT || q->flags ||
	    !all_zero(q->pad, sizeof(q->pad)) || !all_zero(q->reserved, sizeof(q->reserved)))
		return -EINVAL;
	err = order_check(q);
	for (i = 0; i < q->nterms && !err; i++) {
		err = term_check(&q->terms[i], MOOB_TERM_MISSING_OK);
		if (!(q->terms[i].flags & MOOB_TERM_MISSING_OK))
			plain++;
	}
	/* a scan needs one term that only matches carriers of its attribute (spec §4.1) */
	if (!err && !q->order_by[0] && !plain)
		err = -EINVAL;
	return err;
}

/* Runs a validated request (kernel copy) and fills its count, truncated and cursor. */
static long query2_exec(struct super_block *sb, struct file *file, struct moob_query2 *q,
			u64 domain, unsigned int depth)
{
	struct moobl_index *ix = IX(sb);
	struct moobl_qscope sc = { 0 };
	struct moobl_resume *start = NULL, *last = NULL;
	struct moob_query_term *t = NULL;
	u64 qhash;
	u32 i;
	long err;

	sc.domain = domain;
	sc.strip = depth;
	sc.check = READ_ONCE(ix->fallback_all) ||
		   xa_load(&ix->fallback, (unsigned long)domain) != NULL;
	sc.ordered = q->order_by[0] != '\0';
	sc.desc = q->order == MOOB_ORDER_DESC;
	sc.otype = q->order_type;
	err = query_hash(ix, q, domain, sc.check, &qhash);
	if (err)
		return err;
	/* execution copy of the terms: a plain scan must lead with a term without MISSING_OK */
	t = kmemdup(q->terms, sizeof(q->terms), GFP_KERNEL);
	start = kzalloc(sizeof(*start), GFP_KERNEL);
	last = kzalloc(sizeof(*last), GFP_KERNEL);
	if (!t || !start || !last) {
		err = -ENOMEM;
		goto out;
	}
	if (!sc.ordered && (t[0].flags & MOOB_TERM_MISSING_OK)) {
		for (i = 1; i < q->nterms; i++)
			if (!(t[i].flags & MOOB_TERM_MISSING_OK)) {
				swap(t[0], t[i]);
				break;
			}
	}
	sc.scan = sc.ordered ? q->order_by : t[0].name;
	if (!sc.ordered) {
		for (i = 0; i < q->nterms; i++)
			if (t[i].op == MOOB_OP_EQ && !(t[i].flags & MOOB_TERM_MISSING_OK))
				sc.leap_idx[sc.nleap++] = (u8)i;
		sc.leap = sc.nleap >= 2;
		if (sc.leap)
			sc.scan = t[sc.leap_idx[0]].name;	/* cursor key: the lead term */
	}
	if (!all_zero(q->cursor, sizeof(q->cursor))) {
		err = cursor_decode(ix, q->cursor, qhash, sc.scan, &sc, start);
		if (err)
			goto out;
	}
	err = query_run(sb, file, t, q->nterms, q->limit, q->results, &sc, start, last,
			&q->count, &q->truncated);
	if (err)
		goto out;
	memset(q->cursor, 0, sizeof(q->cursor));
	if (q->truncated && last->valid)
		cursor_encode(ix, q->cursor, last, qhash);
out:
	kfree(t);
	kfree(start);
	kfree(last);
	return err;
}

static long do_query2(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moob_query2 *q;
	unsigned int depth;
	u64 domain;
	long err;

	err = query2_target(sb, file, &domain, &depth);
	if (err)
		return err;
	q = memdup_user(arg, sizeof(*q));
	if (IS_ERR(q))
		return PTR_ERR(q);
	err = query2_validate(q);
	if (!err)
		err = query2_exec(sb, file, q, domain, depth);
	if (!err && copy_to_user(arg, q, sizeof(*q)))
		err = -EFAULT;
	kfree(q);
	return err;
}

/* Several QUERY2 requests on one domain: one permission check, all validated first (§4.3). */
static long do_query2_batch(struct super_block *sb, struct file *file, void __user *arg)
{
	struct moob_query2_batch b;
	struct moob_query2 *qs;
	void __user *uq;
	unsigned int depth;
	u64 domain;
	u32 i;
	long err;

	err = query2_target(sb, file, &domain, &depth);
	if (err)
		return err;
	if (copy_from_user(&b, arg, sizeof(b)))
		return -EFAULT;
	if (b.version != MOOB_QUERY2_VERSION || !b.nqueries || b.nqueries > MOOB_QUERY2_BATCH_MAX ||
	    !all_zero(b.reserved, sizeof(b.reserved)))
		return -EINVAL;
	uq = u64_to_user_ptr(b.queries);
	qs = kvmalloc_array(b.nqueries, sizeof(*qs), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;
	if (copy_from_user(qs, uq, b.nqueries * sizeof(*qs))) {
		err = -EFAULT;
		goto out;
	}
	for (i = 0; i < b.nqueries && !err; i++)
		err = query2_validate(&qs[i]);
	for (i = 0; i < b.nqueries && !err; i++)
		err = query2_exec(sb, file, &qs[i], domain, depth);
	if (!err && copy_to_user(uq, qs, b.nqueries * sizeof(*qs)))
		err = -EFAULT;
out:
	kvfree(qs);
	return err;
}

static long do_get_info(struct super_block *sb, void __user *arg)
{
	unsigned int nofs;
	struct moobl_sb *sbi = MOOBL_SB(sb);
	struct moobl_index *ix = &sbi->index;
	struct moob_info info;
	const char *fs = sbi->lower.dentry->d_sb->s_type->name;
	bool admin = ns_capable_noaudit(&init_user_ns, CAP_SYS_ADMIN);

	memset(&info, 0, sizeof(info));
	info.uapi_version = MOOB_UAPI_VERSION;
	info.format_version = MOOB_ATTR_FORMAT_VERSION;
	info.attr_name_max = MOOB_ATTR_NAME_MAX;
	info.attr_string_max = MOOB_ATTR_STRING_MAX;
	info.attr_raw_max = MOOB_ATTR_RAW_MAX;
	info.attr_max_per_file = MOOB_ATTR_MAX_PER_FILE;
	strscpy(info.lower_fs, fs, sizeof(info.lower_fs));
	/* Capabilities are advertised from the lower filesystem type, not probed. */
	if (!strcmp(fs, "btrfs"))
		info.caps |= MOOB_CAP_REFLINK | MOOB_CAP_DATA_CHECKSUMS | MOOB_CAP_SNAPSHOTS;
	else if (!strcmp(fs, "xfs") || !strcmp(fs, "ocfs2") || !strcmp(fs, "bcachefs"))
		info.caps |= MOOB_CAP_REFLINK;
	if (sbi->nolinks)
		info.flags |= MOOB_FLAG_NOLINKS;
	nofs = ix_read_lock(ix);
	if (ix->xattr_ok)
		info.caps |= MOOB_CAP_XATTR;
	if (!ix->enabled)
		info.flags |= MOOB_FLAG_INDEX_OFF;
	if (ix->paths_stale)
		info.flags |= MOOB_FLAG_PATHS_STALE;
	if (ix->incomplete)
		info.flags |= MOOB_FLAG_INDEX_INCOMPLETE;
	info.index_budget = ix->budget;
	info.domain_depth = ix->domain_depth;
	if (ix->domain_depth)
		info.flags |= MOOB_FLAG_DOMAINS;
	/* The counters sum every user's memories: administrators only (ADR-0011, F1). */
	if (admin) {
		info.flags |= MOOB_FLAG_COUNTERS;
		info.index_used = ix->used;
		info.index_entries = ix->entries;
		info.index_inodes = ix->inode_count;
		info.heterogeneous = ix->hetero_count;
	}
	ix_read_unlock(ix, nofs);
	if (admin) {
		unsigned long idx;
		void *v;

		xa_for_each(&ix->fallback, idx, v)
			info.fallback_domains++;
	}
	return copy_to_user(arg, &info, sizeof(info)) ? -EFAULT : 0;
}

long moobl_root_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct super_block *sb = file_inode(file)->i_sb;

	if (cmd == MOOB_IOC_QUERY2)
		return do_query2(sb, file, (void __user *)arg);
	if (cmd == MOOB_IOC_QUERY2_BATCH)
		return do_query2_batch(sb, file, (void __user *)arg);
	if (file->f_path.dentry != sb->s_root)
		return -ENOTTY;
	switch (cmd) {
	case MOOB_IOC_QUERY:
		return do_query(sb, file, (void __user *)arg);
	case MOOB_IOC_GET_INFO:
		return do_get_info(sb, (void __user *)arg);
	default:
		return -ENOTTY;
	}
}
