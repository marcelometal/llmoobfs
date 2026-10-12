// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/iomap.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include "moobfs_format.h"

static int moob_read_at(void *opaque, m_u64 offset, void *out, size_t len)
{
	struct super_block *sb = opaque;
	u8 *p = out;

	while (len) {
		struct buffer_head *bh;
		size_t within = offset & (MOOB_BLOCK - 1);
		size_t take = min_t(size_t, len, MOOB_BLOCK - within);

		bh = sb_bread(sb, offset / MOOB_BLOCK);
		if (!bh)
			return -EIO;
		memcpy(p, bh->b_data + within, take);
		brelse(bh);
		p += take;
		offset += take;
		len -= take;
	}
	return 0;
}

static int moob_iomap_begin(struct inode *inode, loff_t pos, loff_t length,
		unsigned int flags, struct iomap *map, struct iomap *srcmap)
{
	const struct moob_object *o = inode->i_private;

	if (flags & IOMAP_WRITE)
		return -EROFS;
	if (pos < 0 || pos >= moob_align(o->size ? o->size : 1))
		return -EINVAL;
	map->offset = 0;
	map->length = moob_align(o->size ? o->size : 1);
	map->addr = o->offset;
	map->type = IOMAP_MAPPED;
	map->flags = o->references > 1 ? IOMAP_F_SHARED : 0;
	map->bdev = inode->i_sb->s_bdev;
	return 0;
}

static const struct iomap_ops moob_iomap_ops = { .iomap_begin = moob_iomap_begin };

static int moob_read_folio(struct file *file, struct folio *folio)
{
	return iomap_read_folio(folio, &moob_iomap_ops);
}

static void moob_readahead(struct readahead_control *rac)
{
	iomap_readahead(rac, &moob_iomap_ops);
}

static const struct address_space_operations moob_aops = {
	.read_folio = moob_read_folio,
	.readahead = moob_readahead,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};

static int moob_fiemap(struct inode *inode, struct fiemap_extent_info *info,
		u64 start, u64 len)
{
	return iomap_fiemap(inode, info, start, len, &moob_iomap_ops);
}

static const struct inode_operations moob_file_iops = { .fiemap = moob_fiemap };
static const struct file_operations moob_file_ops = {
	.owner = THIS_MODULE,
	.open = generic_file_open,
	.read_iter = generic_file_read_iter,
	.llseek = generic_file_llseek,
};

static struct inode *moob_file_inode(struct super_block *sb, unsigned int index)
{
	struct moob_image *im = sb->s_fs_info;
	struct moob_file *f = &im->files[index];
	struct moob_object *o = &im->objects[f->object_index];
	struct inode *inode = iget_locked(sb, f->ino);

	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	inode->i_mode = S_IFREG | 0444;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	set_nlink(inode, 1);
	i_size_write(inode, o->size);
	inode->i_blocks = moob_align(o->size ? o->size : 1) / 512;
	inode_set_atime(inode, 0, 0);
	inode_set_mtime(inode, 0, 0);
	inode_set_ctime(inode, 0, 0);
	inode->i_op = &moob_file_iops;
	inode->i_fop = &moob_file_ops;
	inode->i_mapping->a_ops = &moob_aops;
	inode->i_private = o;
	unlock_new_inode(inode);
	return inode;
}

static struct dentry *moob_lookup(struct inode *dir, struct dentry *dentry,
		unsigned int flags)
{
	struct moob_image *im = dir->i_sb->s_fs_info;
	unsigned int i;

	for (i = 0; i < im->file_count; i++) {
		if (dentry->d_name.len == strlen(im->files[i].name) &&
		    !memcmp(dentry->d_name.name, im->files[i].name, dentry->d_name.len))
			return d_splice_alias(moob_file_inode(dir->i_sb, i), dentry);
	}
	d_add(dentry, NULL);
	return NULL;
}

static int moob_iterate(struct file *file, struct dir_context *ctx)
{
	struct moob_image *im = file_inode(file)->i_sb->s_fs_info;

	if (!dir_emit_dots(file, ctx))
		return 0;
	while (ctx->pos >= 2 && ctx->pos - 2 < im->file_count) {
		struct moob_file *f = &im->files[ctx->pos - 2];

		if (!dir_emit(ctx, f->name, strlen(f->name), f->ino, DT_REG))
			break;
		ctx->pos++;
	}
	return 0;
}

static const struct inode_operations moob_dir_iops = { .lookup = moob_lookup };
static const struct file_operations moob_dir_ops = {
	.owner = THIS_MODULE,
	.read = generic_read_dir,
	.iterate_shared = moob_iterate,
	.llseek = generic_file_llseek,
};

static int moob_statfs(struct dentry *dentry, struct kstatfs *st)
{
	struct moob_image *im = dentry->d_sb->s_fs_info;

	st->f_type = MOOB_STAT_MAGIC;
	st->f_bsize = MOOB_BLOCK;
	st->f_blocks = im->image_size / MOOB_BLOCK;
	st->f_bfree = st->f_bavail = 0;
	st->f_files = im->file_count + 1;
	st->f_ffree = 0;
	st->f_namelen = MOOB_NAME_MAX;
	return 0;
}

static void moob_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
}

static void moob_put_super(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

static const struct super_operations moob_super_ops = {
	.statfs = moob_statfs,
	.evict_inode = moob_evict_inode,
	.put_super = moob_put_super,
};

static int moob_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct moob_image *im;
	struct inode *root;
	u8 *scratch;
	const char *why;
	int ret;

	if (!sb_set_blocksize(sb, MOOB_BLOCK))
		return -EINVAL;
	im = kzalloc(sizeof(*im), GFP_KERNEL);
	scratch = kmalloc(MOOB_BLOCK, GFP_KERNEL);
	if (!im || !scratch) {
		kfree(im);
		kfree(scratch);
		return -ENOMEM;
	}
	ret = moob_parse(im, bdev_nr_bytes(sb->s_bdev), moob_read_at, sb, scratch, &why);
	kfree(scratch);
	if (ret) {
		pr_warn("moobfs: rejected image: %s\n", why);
		kfree(im);
		return ret == -2 ? -EIO : -EINVAL;
	}
	sb->s_fs_info = im;
	sb->s_magic = MOOB_STAT_MAGIC;
	sb->s_op = &moob_super_ops;
	sb->s_maxbytes = MOOB_MAX_DATA;
	sb->s_flags |= SB_RDONLY | SB_NOATIME;
	root = new_inode(sb);
	if (!root) {
		moob_put_super(sb);
		return -ENOMEM;
	}
	root->i_ino = 1;
	root->i_mode = S_IFDIR | 0555;
	i_uid_write(root, 0);
	i_gid_write(root, 0);
	set_nlink(root, 2);
	inode_set_atime(root, 0, 0);
	inode_set_mtime(root, 0, 0);
	inode_set_ctime(root, 0, 0);
	root->i_op = &moob_dir_iops;
	root->i_fop = &moob_dir_ops;
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		moob_put_super(sb);
		return -ENOMEM;
	}
	return 0;
}

static int moob_get_tree(struct fs_context *fc)
{
	if (!(fc->sb_flags & SB_RDONLY))
		return -EROFS;
	return get_tree_bdev(fc, moob_fill_super);
}

static int moob_reconfigure(struct fs_context *fc)
{
	return fc->sb_flags & SB_RDONLY ? 0 : -EROFS;
}

static const struct fs_context_operations moob_context_ops = {
	.get_tree = moob_get_tree,
	.reconfigure = moob_reconfigure,
};

static int moob_init_context(struct fs_context *fc)
{
	fc->ops = &moob_context_ops;
	return 0;
}

static struct file_system_type moob_type = {
	.owner = THIS_MODULE,
	.name = "moobfs",
	.init_fs_context = moob_init_context,
	.kill_sb = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static int __init moob_init(void) { return register_filesystem(&moob_type); }
static void __exit moob_exit(void) { unregister_filesystem(&moob_type); }
module_init(moob_init);
module_exit(moob_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MoobFS experimental read-only shared-object filesystem");
MODULE_ALIAS_FS("moobfs");
