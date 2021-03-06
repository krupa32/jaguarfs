#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include "jaguar.h"
#include "debug.h"

static int read_sb(struct super_block *sb)
{
	int ret = -EINVAL;
	struct buffer_head *bh;
	struct jaguar_super_block *jsb = NULL;

	DBG("jaguar_sb_read: entering\n");

	/* allocate in-memory super block */
	if ((jsb = kzalloc(sizeof(*jsb), GFP_KERNEL)) == NULL) {
		ERR("no memory\n");
		ret = -ENOMEM;
		goto fail;	
	}

	sb->s_fs_info = jsb;

	/* read the super block from the disk */
	if ((bh = __bread(sb->s_bdev, 0, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading super block from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* store the buffer head, it is never released until put_super.
	 * point the disk_copy to point to the buffer head's data.
	 */
	jsb->bh = bh;
	jsb->disk_copy = (struct jaguar_super_block_on_disk *) bh->b_data;

	DBG("on disk: sb->name=%s\n", jsb->disk_copy->name);

	return 0;

fail:
	if (jsb)
		kfree(jsb);

	return ret;
}

static int jaguar_write_inode(struct inode *i, struct writeback_control *wbc)
{
	DBG("jaguar_write_inode: entering, inum=%d\n", (int)i->i_ino);

	return write_inode_to_disk(i);
}

static void jaguar_put_super(struct super_block *sb)
{
	struct jaguar_super_block *jsb;

	DBG("jaguar_put_super: entering\n");

	jsb = (struct jaguar_super_block *)sb->s_fs_info;

	/* now release the buffer head of the super block */
	brelse(jsb->bh);
}

static int jaguar_statfs(struct dentry *d, struct kstatfs *stat)
{
	struct super_block *sb = d->d_sb;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	u64 id;

	DBG("jaguar_statfs: entering\n");

	jsb = (struct jaguar_super_block *)sb->s_fs_info;
	jsbd = (struct jaguar_super_block_on_disk *)jsb->disk_copy;

	id = huge_encode_dev(sb->s_bdev->bd_dev);

	stat->f_type = JAGUAR_MAGIC;
	stat->f_bsize = sb->s_blocksize;
	stat->f_blocks = jsbd->n_blocks;
	stat->f_bfree = jsbd->n_blocks_free;
	stat->f_bavail = jsbd->n_blocks_free;
	stat->f_files = jsbd->n_inodes;
	stat->f_ffree = jsbd->n_inodes_free;
	stat->f_namelen = strlen(jsbd->name);
	stat->f_fsid.val[0] = (u32)id;
	stat->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

const struct super_operations jaguar_sops = {
	.write_inode		= jaguar_write_inode,
	.put_super		= jaguar_put_super,
	.statfs			= jaguar_statfs
};

/* Supposed to fill the super block related information.
 * Most important info is the s_root field, which points to
 * the inode of the root directory, along with its inode_ops and f_ops.
 */
int jaguar_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret = -EINVAL;
	struct inode *root_inode = NULL;

	DBG("jaguar_fill_super: entering\n");

	/* set block size of super block AND the backing block dev */
	if (!sb_set_blocksize(sb, JAGUAR_BLOCK_SIZE)) {
		ERR("error setting block size\n");
		ret = -EIO;
		goto fail;
	}

	/* read the super block from disk */
	if (read_sb(sb)) {
		ERR("error reading super block from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* setup the super block magic and ops */
	sb->s_magic = JAGUAR_MAGIC;
	sb->s_op = &jaguar_sops;

	/* create the root dir inode of the fs */
	root_inode = jaguar_iget(sb, 1);
	if (IS_ERR(root_inode)) {
		ERR("error allocating root inode\n");
		ret = PTR_ERR(root_inode);
		goto fail;
	}

	/* TODO: is this required? */
	set_nlink(root_inode, 2);

	/* setup the dentry of the root dir in super block */
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ERR("d_make_root failed\n");
		goto fail;
	}

	return 0;

fail:

	return ret;
}


