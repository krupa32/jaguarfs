#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "jaguar.h"
#include "debug.h"

int fill_inode(struct inode *i);

static int logical_to_phys_block(struct inode *i, int logical_block)
{
	int index = logical_block, ret = 0;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;

	if (index < 12) {
		ret = ji->disk_copy.blocks[index];
	} else {
		/* TODO: read from single, double, triple indirect
		 * block, and find the phys block.
		 */
	}

	//DBG("logical_to_phys_block: mapped %d to %d\n", logical_block, ret);
	return ret;
}

static void update_inode_block_map(struct inode *i, int logical_block, int phys_block)
{
	int index = logical_block;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;

	if (index < 12) {
		ji->disk_copy.blocks[index] = phys_block;
	} else {
		/* TODO: update single, double or triple indirect blocks */
	}
}

static int read_inode_info(struct inode *i)
{
	int block, offset, ret = 0;
	struct buffer_head *bh = NULL;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;

	DBG("read_inode_info: entering, inum=%d\n", (int)i->i_ino);

	/* calculate block and offset where inode is present */
	block = BYTES_TO_BLOCK(jsb->disk_copy.inode_tbl_start) + INUM_TO_BLOCK(i->i_ino);
	offset = INUM_TO_OFFSET(i->i_ino);
	//DBG("block = %d, offset = %d\n", block, offset);

	/* read inode from disk */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	memcpy(jid, bh->b_data + offset, sizeof(*jid));

fail:
	if (bh)
		brelse(bh);

	return ret;
}

int write_inode_info(struct inode *i)
{
	int block, offset, ret = 0;
	struct buffer_head *bh = NULL;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;

	DBG("write_inode_info: entering, inum=%d, size=%d\n", (int)i->i_ino, (int)i->i_size);

	/* update some dynamic fields on the disk copy */
	jid->size = i->i_size;

	/* calculate block and offset where inode is present */
	block = BYTES_TO_BLOCK(jsb->disk_copy.inode_tbl_start) + INUM_TO_BLOCK(i->i_ino);
	offset = INUM_TO_OFFSET(i->i_ino);
	//DBG("block = %d, offset = %d\n", block, offset);

	/* read inode from disk */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	memcpy(bh->b_data + offset, jid, sizeof(*jid));
	mark_buffer_dirty(bh);

fail:
	if (bh)
		brelse(bh);

	return ret;
}


static int alloc_inode(struct super_block *sb)
{
	int ret = -1;
	int block;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct buffer_head *bh = NULL;

	jsb = sb->s_fs_info;
	jsbd = &jsb->disk_copy;

	/*
	 * TODO The following code reads only one page of bmap.
	 * It has to take care of condition where bmap can be of
	 * multiple pages, in which case, the super block should track
	 * the next free inum, which can be used as a hint to start reading
	 * from the corresponding page, and then iterate through the pages
	 * of the bmap.
	 *
	 * OR another idea is to forget about any optimization, and ALWAYS
	 * read from the start of the bmap, iterate through the pages and
	 * find a free inode.
	 */

	/* read inode bitmap from disk */
	block = BYTES_TO_BLOCK(jsbd->inode_bmap_start);
	//DBG("alloc_inode: reading inode bmap from block %d\n", block);
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode bmap from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* find the first zero bit, set it to 1 */
	ret = (int)jaguar_find_first_zero_bit(bh->b_data, JAGUAR_BLOCK_SIZE);
	jaguar_set_bit(bh->b_data, ret);
	mark_buffer_dirty(bh);
	DBG("alloc_inode: found inum %d\n", ret);

fail:
	if (bh)
		brelse(bh);
	return ret;
}

static int free_inode(struct inode *i)
{
	int ret = 0;
	int block;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct buffer_head *bh = NULL;
	struct jaguar_inode *ji = (struct jaguar_inode *) i->i_private;

	jsb = sb->s_fs_info;
	jsbd = &jsb->disk_copy;

	/* clear out the inode info on disk */
	memset(&ji->disk_copy, 0, sizeof(ji->disk_copy));
	if (write_inode_info(i)) {
		ERR("error clearing out inode info on disk\n");
		ret = -EIO;
		goto fail;
	}


	/*
	 * TODO The following code reads only one page of bmap.
	 * It has to take care of condition where bmap can be of
	 * multiple pages
	 */

	/* read inode bitmap from disk */
	block = BYTES_TO_BLOCK(jsbd->inode_bmap_start);
	//DBG("free_inode: reading inode bmap from block %d\n", block);
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode bmap from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* find the first zero bit, set it to 1 */
	jaguar_clear_bit(bh->b_data, i->i_ino);
	mark_buffer_dirty(bh);
	DBG("free_inode: freed inum %d\n", (int)i->i_ino);

fail:
	if (bh)
		brelse(bh);
	return ret;
}


static int read_inode_data(struct inode *i, 
		int pos, int size, void *data)
{
	int offset, logical_block, block, ret = 0;
	struct buffer_head *bh = NULL;

	DBG("read_inode_data: entering. inum=%d, pos=%d, size=%d\n", (int)i->i_ino, pos, size);

	if (pos >= i->i_size) {
		ret = -EINVAL;
		goto fail;
	}

	/* convert linear file position to logical block, offset */
	logical_block = pos / JAGUAR_BLOCK_SIZE;
	offset = pos % JAGUAR_BLOCK_SIZE;

	/* convert logical block to physical block.
	 * offset remains the same as logical and phys block size are same
	 */
	block = logical_to_phys_block(i, logical_block);

	/* read a block from disk */
	if ((bh = __bread(i->i_sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* copy the data at offset */
	memcpy(data, bh->b_data + offset, size);

fail:
	if (bh)
		brelse(bh);

	return ret;
}

static int write_inode_data(struct inode *i, 
		int pos, int size, void *data)
{
	int offset, logical_block, block, ret = 0, save_inode = 0;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;
	struct buffer_head *bh = NULL;

	DBG("write_inode_data: entering. inum=%d, pos=%d, size=%d\n", (int)i->i_ino, pos, size);

	/* convert linear file position to logical block, offset */
	logical_block = pos / JAGUAR_BLOCK_SIZE;
	offset = pos % JAGUAR_BLOCK_SIZE;

	/* convert logical block to physical block.
	 * offset remains the same as logical and phys block size are same
	 */
	block = logical_to_phys_block(i, logical_block);

	/* if no block was allocated for this offset, allocate one now */
	if (!block) {
		block = alloc_data_block(i->i_sb);
		if (block < 0) {
			ERR("could not allocate data block\n");
			ret = -ENOMEM;
			goto fail;
		}

		/* store the block in the inode */
		update_inode_block_map(i, logical_block, block);

		save_inode = 1;
	}

	/* read a block from disk */
	if ((bh = __bread(i->i_sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* copy the data to be written at offset in buffer,
	 * and mark buffer dirty
	 */
	memcpy(bh->b_data + offset, data, size);
	mark_buffer_dirty(bh);

	/* check if inode size has to be changed */
	if ((pos + size) > i->i_size) {
		i->i_size = pos + size;
		jid->size = pos + size;
		save_inode = 1;
	}

	if (save_inode) {
		if (write_inode_info(i)) {
			ERR("error writing inode info to disk\n");
			ret = -EIO;
			goto fail;
		}
	}

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

	DBG("jaguar_readdir: entering, inum=%d, pos=%d, isize=%d\n", 
		(int)i->i_ino, (int)filp->f_pos, (int)i->i_size);

	/* read the dir entries from the disk */
	while (filp->f_pos < i->i_size) {

		/* read a dentry from disk */
		if (read_inode_data(i, (int)filp->f_pos, sizeof(jd), &jd)) {
			ERR("error reading dentry from disk\n");
			goto out;
		}

		/* inum 0 is a deleted entry */
		if (jd.inum > 0) {
			/* pass the contents to the caller */
			//DBG("calling filldir with name=[%s], inum=%d\n", jd.name, jd.inum);
			done = filldir(dirent, jd.name, strlen(jd.name), 0, jd.inum, DT_UNKNOWN);

			if (done) {
				/* caller says no more buffer space */
				//DBG("filldir returned non-zero. finishing.\n");
				goto out;
			}
		}

		filp->f_pos += sizeof(jd);
	}

out:
	//DBG("jaguar_readdir: leaving\n");
	return 0;
}	

static int create_file_dir(struct inode *parent, 
		struct dentry *d, int type, struct inode **newinode)
{
	int inum, ret = 0, pos;
	struct inode *i;
	struct jaguar_dentry_on_disk jd;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("create_file_dir: entering: name=%s, type=%d\n", 
			d->d_name.name, type);

	/* alloc a new inode on disk */
	inum = alloc_inode(parent->i_sb);
	if (inum < 0) {
		ERR("could not allocate inode on disk\n");
		ret = -ENOMEM;
		goto fail;
	}

	i = jaguar_iget(parent->i_sb, inum);
	if (!i) {
		ERR("could not get new inode\n");
		ret = -ENOMEM;
		goto fail;
	}

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	jid->type = type;
	jid->nlink = 1;

	if (write_inode_info(i)) {
		ERR("error updating inode info\n");
		ret = -EIO;
		goto fail;
	}

	/* find a new empty dentry under the parent dir */
	pos = 0;
	while (pos < parent->i_size) {
		if (read_inode_data(parent, pos, sizeof(jd), &jd)) {
			ERR("error reading dentry at offset %d\n", pos);
			ret = -EIO;
			goto fail;
		}

		if (jd.inum == 0)
			break;

		pos += sizeof(jd);
	}


	/* pos now points to an empty intermediate dentry, or it points to
	 * end of directory entries. in either case, fill the new dentry 
	 * pointed by pos with d->d_name.name and inum, and save it.
	 */
	//DBG("new dentry at pos=%d\n", pos);
	jd.inum = inum;
	strcpy(jd.name, d->d_name.name);
	if (write_inode_data(parent, pos, sizeof(jd), &jd)) {
		ERR("error writing out new dentry\n");
		ret = -EIO;
		goto fail;
	}

	/* update the parent's nlink and save it on disk */
	inode_inc_link_count(parent);
	ji = (struct jaguar_inode *) parent->i_private;
	ji->disk_copy.nlink++;
	if (write_inode_info(parent)) {
		ERR("error updating parent nlink\n");
		ret = -EIO;
		goto fail;
	}

	/* now that all disk data is updated, re-read the inode from
	 * disk and update the in-core inode object
	 */
	fill_inode(i);

	/* note: if you do not d_instantiate the dentry here, vfs will
	 * call 'lookup' immediately after this to try to associate an
	 * inode. somehow that doesnt work, and ends up in the dentry
	 * not getting removed even after rmdir. not enough time to look
	 * at that problem.
	 */
	d_instantiate(d, i);

	if (newinode)
		*newinode = i;

fail:
	return ret;


}

static int unlink_file_dir(struct inode *parent, struct dentry *d)
{
	int block, ret = 0, pos;
	struct inode *i = d->d_inode;
	struct jaguar_dentry_on_disk jd;
	struct jaguar_inode *ji;

	DBG("unlink_file_dir: entering: name=%s\n", d->d_name.name);

	/* free the data blocks associated with inode
	 * TODO: handle the case when there are multiple data blocks
	 */
	ji = (struct jaguar_inode *) i->i_private;
	block = ji->disk_copy.blocks[0];
	if (free_data_block(i->i_sb, block)) {
		ERR("could not free dir inode data blocks\n");
		ret = -EIO;
		goto fail;
	}

	/* free the inode on disk */
	if (free_inode(i)) {
		ERR("could not free inode on disk\n");
		ret = -EIO;
		goto fail;
	}

	/* find the dentry under the parent dir */
	pos = 0;
	while (pos < parent->i_size) {
		if (read_inode_data(parent, pos, sizeof(jd), &jd)) {
			ERR("error reading dentry at offset %d\n", pos);
			ret = -EIO;
			goto fail;
		}

		if (strcmp(d->d_name.name, jd.name) == 0) {
			break;
		}

		pos += sizeof(jd);
	}

	/* remove the dentry of inode from the parent dir */
	jd.inum = 0;
	memset(jd.name, 0, sizeof(jd.name));
	if (write_inode_data(parent, pos, sizeof(jd), &jd)) {
		ERR("error clearing out dentry\n");
		ret = -EIO;
		goto fail;
	}

	/* update the parent's nlink and save it on disk */
	inode_dec_link_count(parent);
	ji = (struct jaguar_inode *) parent->i_private;
	ji->disk_copy.nlink--;
	if (write_inode_info(parent)) {
		ERR("error updating parent nlink\n");
		ret = -EIO;
		goto fail;
	}

	/* decrement child's nlink. no need to update on disk, since
	 * it has already been freed. but the decrement is required,
	 * otherwise the inode may never be removed from the inode cache.
	 */
	inode_dec_link_count(i);
	DBG("i->i_nlink=%d\n", i->i_nlink);
fail:
	return ret;

}

static int jaguar_mkdir(struct inode *parent, struct dentry *d, umode_t mode)
{
	int ret = 0, pos;
	struct inode *i;
	struct jaguar_dentry_on_disk jd;

	DBG("jaguar_mkdir: entering: name=%s\n", d->d_name.name);

	if (create_file_dir(parent, d, INODE_TYPE_DIR, &i) < 0)
		goto fail;

	/* add '.' and '..' for the new inode */
	pos = 0;
	memset(&jd, 0, sizeof(jd));
	jd.inum = i->i_ino;
	strcpy(jd.name, ".");
	if (write_inode_data(i, pos, sizeof(jd), &jd)) {
		ERR("error writing dentry for .\n");
		ret = -EIO;
		goto fail;
	}
	pos += sizeof(jd);
	memset(&jd, 0, sizeof(jd));
	jd.inum = parent->i_ino;
	strcpy(jd.name, "..");
	if (write_inode_data(i, pos, sizeof(jd), &jd)) {
		ERR("error writing dentry for ..\n");
		ret = -EIO;
		goto fail;
	}

fail:
	return ret;
}


static int jaguar_rmdir(struct inode *parent, struct dentry *d)
{
	DBG("jaguar_rmdir: entering, name=%s\n", d->d_name.name);

	return unlink_file_dir(parent, d);
}


/* Supposed to find the inode corresponding to d->d_name.name, iget the
 * corresponding inode, and map the 'd' to the inode using d_add().
 */
static struct dentry *jaguar_lookup(struct inode *parent, struct dentry *d, unsigned int flags)
{
	int pos = 0, ret = 0;
	struct jaguar_dentry_on_disk jd;
	struct inode *i;

	DBG("jaguar_lookup: entering, name=%s\n", d->d_name.name);

	while (pos < parent->i_size) {
		/* read a dentry */
		if (read_inode_data(parent, pos, sizeof(jd), &jd)) {
			ERR("error reading dentry at offset %d\n", pos);
			ret = -EIO;
			goto fail;
		}

		//DBG("comparing dentry %s with %s\n", jd.name, d->d_name.name);
		if (strcmp(d->d_name.name, jd.name) == 0) {

			//DBG("match found... mapping %s to inode %d\n", d->d_name.name, jd.inum);
			/* get an inode */
			i = jaguar_iget(parent->i_sb, jd.inum);
			//DBG("inum=%d, count=%d\n", (int)i->i_ino, atomic_read(&i->i_count));

			/* associate given dentry with the inode */
			//DBG("adding inode %p with dentry %s\n", i, d->d_name.name);
			d_add(d, i);

			break;
		}

		pos += sizeof(jd);
	}


fail:
	//DBG("jaguar_lookup: leaving\n");
	return NULL;
}


static int jaguar_create(struct inode *parent, 
		struct dentry *d, umode_t mode, bool excl)
{
	DBG("jaguar_create: entering: name=%s\n", d->d_name.name);

	return create_file_dir(parent, d, INODE_TYPE_FILE, NULL);
}

static int jaguar_unlink(struct inode *parent, struct dentry *d)
{
	DBG("jaguar_rmdir: entering: name=%s\n", d->d_name.name);

	return unlink_file_dir(parent, d);
}

static int jaguar_get_block(struct inode *i, sector_t logical_block,
		struct buffer_head *bh, int create)
{
	int block, ret = 0;

	DBG("jaguar_get_block: entering. inum=%d, block=%d, create=%d\n", (int)i->i_ino, (int)logical_block, create);

	/* convert logical block to physical block.
	 */
	block = logical_to_phys_block(i, (int)logical_block);

	/* if no block was allocated for this offset, allocate one now */
	if (!block) {
		block = alloc_data_block(i->i_sb);
		if (block < 0) {
			ERR("could not allocate data block\n");
			ret = -ENOMEM;
			goto fail;
		}

		/* store the block in the inode */
		update_inode_block_map(i, logical_block, block);

		/* update the inode on disk */
		if (write_inode_info(i)) {
			ERR("error writing inode info to disk\n");
			ret = -EIO;
			goto fail;
		}

		set_buffer_new(bh);

	} else {
	}

	set_buffer_mapped(bh);
	bh->b_bdev = i->i_sb->s_bdev;
	bh->b_blocknr = block;
	bh->b_size = JAGUAR_BLOCK_SIZE;
	DBG("mapped logical block %d to phys block %d\n", 
			(int)logical_block, block);

fail:
	return ret;
}

static int jaguar_readpage(struct file *filp, struct page *page)
{
	DBG("jaguar_readpage: entering\n");

	return block_read_full_page(page, jaguar_get_block);
}

static int jaguar_writepage(struct page *page, struct writeback_control *wbc)
{
	DBG("jaguar_writepage: entering\n");

	return block_write_full_page(page, jaguar_get_block, wbc);
}

static int jaguar_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, 
		struct page **pagep, void **fsdata)
{
	DBG("jaguar_write_begin: entering\n");

	return block_write_begin(mapping, pos, len, 
			flags, pagep, jaguar_get_block);
}




static const struct inode_operations jaguar_inode_ops = {
	.lookup		= jaguar_lookup,
	.mkdir		= jaguar_mkdir,
	.rmdir		= jaguar_rmdir,
	.create		= jaguar_create,
	.unlink		= jaguar_unlink
};

static const struct file_operations jaguar_file_ops = {
	.readdir	= jaguar_readdir,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.llseek		= generic_file_llseek
};

static const struct address_space_operations jaguar_aops = {
	.readpage	= jaguar_readpage,
	.writepage	= jaguar_writepage,
	.write_begin	= jaguar_write_begin,
	.write_end	= generic_write_end
};

/* Reads a disk inode with number i->i_ino, and fills 'i' with the info.
 */
int fill_inode(struct inode *i)
{
	int ret = 0;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;

	DBG("fill_inode: entering, inum=%d\n", (int)i->i_ino);

	/* read the inode from disk */
	if (read_inode_info(i)) {
		ERR("error reading inode info from disk\n");
		ret = -EIO;
		goto fail;
	}
	//DBG("inode %d: size=%d, type=%d\n", (int)i->i_ino, ji->disk_copy.size, ji->disk_copy.type);

	/* setup the inode fields */
	i->i_size = ji->disk_copy.size;
	set_nlink(i, ji->disk_copy.nlink);
	if (ji->disk_copy.type == INODE_TYPE_FILE)
		i->i_mode = S_IFREG | 0777;
	else if (ji->disk_copy.type == INODE_TYPE_DIR) {
		i->i_mode = S_IFDIR | 0777;
	}
	i->i_op = &jaguar_inode_ops;
	i->i_fop = &jaguar_file_ops;
	i->i_mapping->a_ops = &jaguar_aops;

fail:
	return ret;
}

struct inode * jaguar_iget(struct super_block *sb, int inum)
{
	struct inode *i;
	struct jaguar_inode *ji = NULL;

	DBG("jaguar_iget: entering inum=%d\n", inum);

	i = iget_locked(sb, inum);
	if (!(i->i_state & I_NEW))
		return i;

	/* inode is fresh, allocate jaguar private data */
	if ((ji = kzalloc(sizeof(*ji), GFP_KERNEL)) == NULL) {
		ERR("error allocating mem\n");
		goto fail;
	}

	i->i_private = ji;

	/* read inode info from disk */
	if (fill_inode(i)) {
		ERR("error reading inode info\n");
		goto fail;
	}

	unlock_new_inode(i);

	return i;

fail:
	if (ji)
		kfree(ji);

	return NULL;
}

