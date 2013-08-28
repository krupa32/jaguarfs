#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "jaguar.h"
#include "debug.h"

/* Supposed to find the inode corresponding to d->d_name.name, iget the
 * corresponding inode, and map the 'd' to the inode using d_add().
 */
static struct dentry *jaguar_lookup(struct inode *dir, struct dentry *d, unsigned int flags)
{
	DBG("jaguar_lookup: entering\n");
	DBG("name=%s\n", d->d_name.name);

	if (strcmp(d->d_name.name, ".") == 0)
		d_add(d, dir);

	DBG("jaguar_lookup: leaving\n");
	return NULL;
}

static int logical_to_phys_block(struct jaguar_inode *ji, int logical_block)
{
	int index = logical_block, ret = 0;

	if (index < 12) {
		ret = ji->disk_copy.blocks[index];
	} else {
		/* TODO: read from single, double, triple indirect
		 * block, and find the phys block.
		 */
	}

	DBG("logical_to_phys_block: mapped %d to %d\n", logical_block, ret);
	return ret;
}

static int read_dentry(struct inode *i, 
		int pos, struct jaguar_dentry_on_disk *jd)
{
	int offset, logical_block, block, ret = 0;
	struct buffer_head *bh;

	DBG("read_dentry: entering. pos=%d\n", pos);

	/* convert linear file position to logical block, offset */
	logical_block = pos / JAGUAR_BLOCK_SIZE;
	offset = pos % JAGUAR_BLOCK_SIZE;

	/* convert logical block to physical block.
	 * offset remains the same as logical and phys block size are same
	 */
	block = logical_to_phys_block(i->i_private, logical_block);

	/* read a block from disk */
	if ((bh = __bread(i->i_sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* copy the dentry at offset */
	memcpy(jd, bh->b_data + offset, sizeof(*jd));

fail:
	if (bh)
		brelse(bh);

	return ret;
}

/* This is called by getdents syscall, which is called by ls.
 * Supposed to read from the filp->f_pos offset of the directory file,
 * and fill in dirent entries using the filldir callback fn.
 * dirent entries are filled as long as filldir returns 0.
 * Note that filp->f_pos has to be updated by this function to reflect
 * how far in the dir we have proceeded.
 * Also, filldir fn ptr is usually compat_filldir.
 * Note: When root inode number was set as 0 and returned, the upper layers
 *       did not display the entry. The upper layers does not like inum 0.
 */
static int jaguar_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	int done = 0;
	struct inode *i = filp->f_dentry->d_inode;
	struct jaguar_dentry_on_disk jd;

	DBG("jaguar_readdir: entering, pos=%d, isize=%d\n", 
		(int)filp->f_pos, (int)i->i_size);

	/* read the dir entries from the disk */
	while (filp->f_pos < i->i_size) {

		/* read a dentry from disk */
		if (read_dentry(i, (int)filp->f_pos, &jd)) {
			ERR("error reading dentry from disk\n");
			goto out;
		}

		/* pass the contents to the caller */
		DBG("calling filldir with name=[%s], inum=%d\n", jd.name, jd.inum);
		done = filldir(dirent, jd.name, strlen(jd.name), 0, jd.inum, DT_UNKNOWN);

		if (done) {
			/* caller says no more buffer space */
			DBG("filldir returned non-zero. finishing.\n");
			goto out;
		}

		filp->f_pos += sizeof(jd);
	}

out:
	DBG("jaguar_readdir: leaving\n");
	return 0;
}	

static const struct inode_operations jaguar_dir_inode_ops = {
	.lookup		= jaguar_lookup
};

static const struct file_operations jaguar_dir_file_ops = {
	.readdir	= jaguar_readdir
};


/* Reads a disk inode with number i->i_ino, and fills 'i' with the info.
 * Also, allocates a jaguar_inode and stores a copy of the disk inode in it.
 */
int jaguar_inode_read(struct inode *i)
{
	int block, offset, ret = -EINVAL;
	struct buffer_head *bh;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;
	struct jaguar_inode *ji = NULL;

	DBG("jaguar_inode_read: entering, inum=%d\n", (int)i->i_ino);

	/* allocate jaguar inode */
	if ((ji = kzalloc(sizeof(*ji), GFP_KERNEL)) == NULL) {
		ERR("error allocating mem\n");
		ret = -ENOMEM;
		goto fail;
	}

	i->i_private = ji;

	/* calculate block and offset where inode is present */
	block = BYTES_TO_BLOCK(jsb->disk_copy.inode_tbl_start) + INUM_TO_BLOCK(i->i_ino);
	offset = INUM_TO_OFFSET(i->i_ino);
	DBG("block = %d, offset = %d\n", block, offset);

	/* read inode from disk */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	memcpy(&ji->disk_copy, bh->b_data + offset, sizeof(ji->disk_copy));
	DBG("inode %d: size=%d, type=%d\n", (int)i->i_ino, ji->disk_copy.size, ji->disk_copy.type);

	/* setup the inode fields */
	i->i_size = ji->disk_copy.size;
	if (ji->disk_copy.type == INODE_TYPE_FILE)
		i->i_mode = S_IFREG | 0755;
	else if (ji->disk_copy.type == INODE_TYPE_DIR) {
		i->i_mode = S_IFDIR | 0755;
		i->i_op = &jaguar_dir_inode_ops;
		i->i_fop = &jaguar_dir_file_ops;
	}

	brelse(bh);

	return 0;

fail:
	if (ji)
		kfree(ji);

	return ret;
}
