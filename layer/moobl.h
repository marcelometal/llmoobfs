/* SPDX-License-Identifier: GPL-2.0-only */
/* Shared declarations of the MoobFS memory layer (main.c and index.c). */
#ifndef MOOBL_H
#define MOOBL_H
#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/xarray.h>
#include <linux/rwsem.h>
#include <linux/path.h>
#include <linux/cred.h>
#include <linux/siphash.h>

#define MOOBL_SUPER_MAGIC 0x4d4f4f42	/* "MOOB" */
#define MOOBL_NAME "moobfs_layer"
#define MOOBL_INDEX_BUDGET_DEFAULT (128ULL << 20)

struct moobl_index {
	struct rw_semaphore lock;
	struct rb_root names;		/* struct moobl_ix_name, by attribute name */
	struct xarray inodes;		/* ino -> struct moobl_ix_inode */
	u64 used, budget;		/* bytes accounted to the index */
	u32 entries, inode_count;
	bool enabled;			/* false with noindex= */
	bool paths_stale;		/* a directory was renamed since the scan */
	bool xattr_ok;			/* lower root answered listxattr */
	bool incomplete;		/* an index update failed after a lower write */
	bool initialized;		/* moobl_index_init() ran for this superblock */
	unsigned int domain_depth;	/* depth of domain directories below the root; 0: off */
	struct xarray fallback;		/* domain ino -> value: answer with per-result checks */
	u32 hetero_count;		/* indexed inodes failing the homogeneity rule */
	siphash_key_t cursor_key;	/* per-mount key authenticating QUERY2 cursors */
};

struct moobl_sb {
	struct path lower;		/* lower root */
	const struct cred *creds;	/* mounter credentials for delegated work */
	bool nolinks;			/* refuse link and symlink creation */
	struct moobl_index index;
};

struct moobl_inode {
	struct inode vfs_inode;
	struct inode *lower;
};

struct moobl_dentry {
	struct path lower;
	struct rcu_head rcu;
};

static inline struct moobl_sb *MOOBL_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct moobl_inode *MOOBL_I(struct inode *inode)
{
	return container_of(inode, struct moobl_inode, vfs_inode);
}

static inline struct inode *moobl_lower_inode(struct inode *inode)
{
	return MOOBL_I(inode)->lower;
}

static inline struct path *moobl_lower_path(struct dentry *dentry)
{
	return &((struct moobl_dentry *)dentry->d_fsdata)->lower;
}

static inline struct dentry *moobl_lower_dentry(struct dentry *dentry)
{
	return moobl_lower_path(dentry)->dentry;
}

/* index.c */
void moobl_index_init(struct super_block *sb, u64 budget, bool enabled, unsigned int domain_depth);
int moobl_index_build(struct super_block *sb);
void moobl_index_destroy(struct super_block *sb);
int moobl_index_set(struct super_block *sb, struct dentry *dentry, const char *name,
		    const void *value, size_t size);
void moobl_index_remove(struct super_block *sb, u64 ino, const char *name);
void moobl_index_drop_inode(struct super_block *sb, u64 ino);
void moobl_index_renamed(struct super_block *sb, struct inode *inode, struct dentry *where);
void moobl_index_inode_cached(struct super_block *sb, struct inode *inode);
void moobl_index_inode_evicted(struct super_block *sb, struct inode *inode);
long moobl_root_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
void moobl_index_perm_changed(struct super_block *sb, struct dentry *dentry);
bool moobl_rename_crosses_domain(struct super_block *sb, struct dentry *old_dentry,
				 struct dentry *new_dentry);

#endif
