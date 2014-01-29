#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mount.h>
#include <linux/highmem.h>
#include "jaguar.h"
#include "debug.h"

int fill_inode(struct inode *i);
long jaguar_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int logical_to_phys_block(struct inode *i, int logical_block)
{
	int index, level, block, *block_map, block_index, ret = 0;
	int max_blks_at_level[] = { 12, 1024, 1048576 };
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct super_block *sb = i->i_sb;
	struct buffer_head *bh;

	//DBG("logical_to_phys_block: entering log=%d\n", logical_block);

	index = logical_block;
	level = 0;
	while (index >= max_blks_at_level[level]) {
		index -= max_blks_at_level[level];
		level++;
	}
	//DBG("found level = %d, index = %d\n", level, index);

	/* now index corresponds to index WITHIN that level of indirection.
	 * ie, if logical block is 1040, level = 2, index = 4.
	 */

	/* handle the simplest case first: no indirection */
	if (level == 0) {
		ret = ji->disk_copy.blocks[index];
		goto out;
	}

	/* check whether an indirect block is allocated for 'level' */
	block = ji->disk_copy.blocks[11 + level];
	if (!block)
		goto out;

	/* read the indirect block into mem */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not read indirect block\n");
		goto out;
	}

	block_map = (int *)bh->b_data;

	while (level > 1) {
		
		DBG("level %d: index=%d\n", level, index);

		/* indirection level is more than 1.
		 * find out the index of the block which has the next level
		 * of indirect blocks.
		 */
		block_index = index / max_blks_at_level[level - 1];

		/* find out the index of the block for the next level */
		index = index % max_blks_at_level[level-1];

		block = block_map[block_index];
		if (!block)
			goto out;

		/* read the next level indirect block into mem */
		brelse(bh);
		if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
			ERR("could not read indirect block\n");
			goto out;
		}

		block_map = (int *)bh->b_data;

		level--;
	}

	/* now we are at level 1 indirection */
	ret = block_map[index];
	brelse(bh);

out:
	DBG("logical_to_phys_block: mapped %d to %d\n", logical_block, ret);
	return ret;
}

/* currently supports only levels 0, 1, 2 of indirection.
 * triple indirect block not yet tested.
 * shouldnt be difficult, though.
 */
static void update_inode_block_map(struct inode *i, int logical_block, int phys_block)
{
	int index, level, save_inode, block, *block_map, block_index;
	int max_blks_at_level[] = { 12, 1024, 1048576 };
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct super_block *sb = i->i_sb;
	struct buffer_head *bh;

	DBG("update_inode_block_map: entering log=%d, phys=%d\n", 
		logical_block, phys_block);

	index = logical_block;
	level = 0;
	while (index >= max_blks_at_level[level]) {
		index -= max_blks_at_level[level];
		level++;
	}
	DBG("found level = %d, index = %d\n", level, index);

	/* now index corresponds to index WITHIN that level of indirection.
	 * ie, if logical block is 1040, level = 2, index = 4.
	 */

	/* handle the simplest case first: no indirection */
	if (level == 0) {
		ji->disk_copy.blocks[index] = phys_block;
		save_inode = 1;
		goto out;
	}

	/* check whether an indirect block is allocated for 'level' */
	block = ji->disk_copy.blocks[11 + level];
	if (!block) {
		if ((block = alloc_data_block(i->i_sb)) < 0) {
			ERR("could not allocate data block\n");
			goto fail;
		}
		ji->disk_copy.blocks[11 + level] = block;
		save_inode = 1;
		DBG("allocated blk %d for inode indirect blk\n", block);
	}

	/* read the indirect block into mem */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not read indirect block\n");
		goto fail;
	}

	block_map = (int *)bh->b_data;

	while (level > 1) {
		
		DBG("level %d: index=%d\n", level, index);

		/* indirection level is more than 1.
		 * find out the index of the block which has the next level
		 * of indirect blocks.
		 */
		block_index = index / max_blks_at_level[level - 1];

		/* find out the index of the block for the next level */
		index = index % max_blks_at_level[level-1];

		block = block_map[block_index];
		if (!block) {
			if ((block = alloc_data_block(i->i_sb)) < 0) {
				ERR("could not allocate data block\n");
				goto fail;
			}
			block_map[block_index] = block;
			mark_buffer_dirty(bh);
			DBG("allocated blk %d for indirect block\n", block);
		}

		/* read the next level indirect block into mem */
		brelse(bh);
		if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
			ERR("could not read indirect block\n");
			goto fail;
		}

		block_map = (int *)bh->b_data;

		level--;
	}

	/* now we are at level 1 indirection */
	block_map[index] = phys_block;
	mark_buffer_dirty(bh);
	brelse(bh);
	DBG("updated level 1 index %d with phys block\n", index);

out:
	if (save_inode)
		mark_inode_dirty(i);
fail:
	return;
}

static int read_inode_from_disk(struct inode *i)
{
	int block, offset, ret = 0;
	struct buffer_head *bh = NULL;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;

	DBG("read_inode_from_disk: entering, inum=%d\n", (int)i->i_ino);

	/* calculate block and offset where inode is present */
	block = BYTES_TO_BLOCK(jsb->disk_copy->inode_tbl_start) + INUM_TO_BLOCK(i->i_ino);
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

int write_inode_to_disk(struct inode *i)
{
	int block, offset, ret = 0;
	struct buffer_head *bh = NULL;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;
	struct jaguar_inode *ji = (struct jaguar_inode *)i->i_private;
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;

	DBG("write_inode_to_disk: entering, inum=%d, size=%d\n", (int)i->i_ino, (int)i->i_size);

	/* update some dynamic fields on the disk copy */
	jid->size = i->i_size;

	/* calculate block and offset where inode is present */
	block = BYTES_TO_BLOCK(jsb->disk_copy->inode_tbl_start) + INUM_TO_BLOCK(i->i_ino);
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
	int ret = -1, inum;
	int block, start, offset, bmap_size, bmap_start;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct buffer_head *bh = NULL;

	jsb = sb->s_fs_info;
	jsbd = jsb->disk_copy;

	if (jsbd->n_inodes_free == 0) {
		ERR("no more inodes available\n");
		ret = -ENOMEM;
		goto fail;
	}

	bmap_start = BYTES_TO_BLOCK(jsbd->inode_bmap_start);
	bmap_size = BYTES_TO_BLOCK(jsbd->inode_bmap_size);
	start = jsbd->next_free_inode / NUM_BITS_PER_BLOCK;

	/* allocate an inode from the bitmap */
	inum = jaguar_bmap_alloc_bit(sb->s_bdev, 
		bmap_start, bmap_size, start);
	if (inum < 0) {
		ERR("could not alloc inum\n");
		ret = -ENOMEM;
		goto fail;
	}
	DBG("alloc_inode: found inum %d\n", inum);

	/* update super block info */
	jsbd->n_inodes_free--;
	jsbd->next_free_inode = (inum + 1) % jsbd->n_inodes;
	mark_buffer_dirty(jsb->bh);

	/* zero out the allocated inode */
	block = BYTES_TO_BLOCK(jsbd->inode_tbl_start) +
		(inum / JAGUAR_NUM_INODES_PER_BLOCK);
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode table from disk\n");
		ret = -EIO;
		goto fail;
	}
	offset = (inum % JAGUAR_NUM_INODES_PER_BLOCK) * JAGUAR_INODE_SIZE;
	memset(bh->b_data + offset, 0, JAGUAR_INODE_SIZE);
	mark_buffer_dirty(bh);

	ret = inum;

fail:
	if (bh)
		brelse(bh);
	return ret;
}

static int free_inode(struct inode *i)
{
	int ret = 0;
	int bmap_start;
	struct super_block *sb = i->i_sb;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct jaguar_inode *ji = (struct jaguar_inode *) i->i_private;

	jsb = sb->s_fs_info;
	jsbd = jsb->disk_copy;

	bmap_start = BYTES_TO_BLOCK(jsbd->inode_bmap_start);

	/* clear out the inode info on disk */
	memset(&ji->disk_copy, 0, sizeof(ji->disk_copy));
	mark_inode_dirty(i);

	/* update the inode bitmap */
	ret = jaguar_bmap_free_bit(sb->s_bdev, bmap_start, i->i_ino);
	if (ret < 0) {
		ERR("error updating inode bitmap\n");
		ret = -EIO;
		goto fail;
	}
	DBG("free_inode: freed inode %d\n", (int)i->i_ino);

	/* update super block info */
	jsbd->n_inodes_free++;
	mark_buffer_dirty(jsb->bh);

fail:
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

	if (save_inode)
		mark_inode_dirty(i);

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
	jid->size = 0;
	jid->type = type;
	jid->nlink = 1;

	/* the inode CANNOT be marked dirty here, because it is read from
	 * disk again few lines below using fill_inode. so, the inode has
	 * to be written to disk HERE.
	 */
	if (write_inode_to_disk(i)) {
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
	BUG_ON(strlen(d->d_name.name) >= JAGUAR_FILENAME_MAX);
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
	mark_inode_dirty(parent);

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

static int free_indirect_block(struct inode *inode, int block, int level)
{
	int ret = 0, i, *block_map;
	struct super_block *sb = inode->i_sb;
	struct buffer_head *bh = NULL;

	DBG("free_indirect_block: entering, level=%d\n", level);

	/* read the indirect block into mem */
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not read indirect block\n");
		ret = -EIO;
		goto out;
	}
	block_map = (int *)bh->b_data;

	for (i = 0; i < JAGUAR_BLOCK_SIZE / 4; i++) {
		if (block_map[i] == 0)
			continue;

		if (level == 1)
			ret = free_data_block(inode->i_sb, block_map[i]);
		else
			ret = free_indirect_block(inode, block_map[i], level-1);

		if (ret)
			goto out;
	}
	
	/* all data blocks in lower levels have been freed.
	 * now free this indirect block.
	 */
	ret = free_data_block(inode->i_sb, block);
out:
	if (bh)
		brelse(bh);

	return ret;
}

static int free_all_data_blocks(struct inode *inode)
{
	int n_blocks, ret = 0, block, i, n_blocks_freed = 0, level;
	struct jaguar_inode *ji = (struct jaguar_inode *)inode->i_private;
	int max_blks_at_level[] = { 12, 1024, 1048576 };

	n_blocks = (inode->i_size + JAGUAR_BLOCK_SIZE - 1 ) / JAGUAR_BLOCK_SIZE;
	DBG("free_all_data_blocks: freeing %d blocks\n", n_blocks);

	i = 0;
	while (n_blocks_freed < n_blocks) {

		block = ji->disk_copy.blocks[i];
		level = (i < 12) ? 0 : i - 11;

		if (level == 0) {
			/* direct block, simply free */
			ret = free_data_block(inode->i_sb, block);
			n_blocks_freed++;
		} else {
			/* indirect block, recursive free */
			ret = free_indirect_block(inode, block, level);
			n_blocks_freed += max_blks_at_level[level];
		}

		if (ret)
			break;

		i++;
	}

	return ret;
}

static int unlink_file_dir(struct inode *parent, struct dentry *d)
{
	int ret = 0, pos;
	struct inode *i = d->d_inode;
	struct jaguar_dentry_on_disk jd;
	struct jaguar_inode *ji;

	DBG("unlink_file_dir: entering: name=%s\n", d->d_name.name);

	/* free all data blocks associated with inode */
	if (free_all_data_blocks(i)) {
		ERR("could not free all data blocks on disk\n");
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
	mark_inode_dirty(parent);

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
static struct dentry *jaguar_lookup(struct inode *parent, struct dentry *d, struct nameidata *data)
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
		struct dentry *d, umode_t mode, struct nameidata *data)
{
	DBG("jaguar_create: entering: name=%s\n", d->d_name.name);

	return create_file_dir(parent, d, INODE_TYPE_FILE, NULL);
}

static int jaguar_unlink(struct inode *parent, struct dentry *d)
{
	DBG("jaguar_rmdir: entering: name=%s\n", d->d_name.name);

	return unlink_file_dir(parent, d);
}

int alloc_version_meta_block(struct inode *i)
{
	int ret = 0, old_ver_meta_block;
	struct super_block *sb;
	struct jaguar_version_metadata *jvm = NULL;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("entering alloc_version_meta_block: inum=%d\n", (int)i->i_ino);

	sb = i->i_sb;
	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	
	old_ver_meta_block = jid->ver_meta_block;

	/* alloc a new data block for version metadata */
	if ((jid->ver_meta_block = alloc_data_block(sb)) < 0) {
		ERR("could not allocate data block\n");
		return -ENOMEM;
		goto fail;
	}

	DBG("allocated new version meta block %d\n", jid->ver_meta_block);

	mark_inode_dirty(i);

	/* get a buffer head for the new metadata block */
	if ((ji->ver_meta_bh = __getblk(sb->s_bdev, jid->ver_meta_block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not get buffer head\n");
		return -ENOMEM;
		goto fail;
	}
	set_buffer_uptodate(ji->ver_meta_bh);

	/* init the version metadata block and write out to disk */
	jvm = (struct jaguar_version_metadata *) ji->ver_meta_bh->b_data;
	memset(jvm, 0, sizeof(*jvm));
	jvm->next_block = old_ver_meta_block;
	mark_buffer_dirty(ji->ver_meta_bh);
	brelse(ji->ver_meta_bh);
	ji->ver_meta_bh = NULL;

	DBG("initialized version meta buffer on disk\n");

fail:
	return ret;
}

static void version(struct inode *i, int logical_block, char *data)
{
	struct buffer_head *ver_bh;
	struct super_block *sb;
	struct jaguar_version_metadata *jvm = NULL;
	struct jaguar_version_metadata_entry *jvme;
	struct jaguar_inode *ji;
	int ver_block;
	struct timeval tv;

	DBG("version: entering: inum=%d, logical=%d\n",
		(int)i->i_ino, logical_block);

	sb = i->i_sb;

	/* allocate a new version data block to store old data */
	if ((ver_block = alloc_data_block(sb)) < 0) {
		ERR("could not allocate version data block\n");
		goto fail;
	}

	/* get buffer head for the version data block */
	if ((ver_bh = __getblk(sb->s_bdev, ver_block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not get buffer head for version block\n");
		goto fail;
	}
	set_buffer_uptodate(ver_bh);

	/* copy the original block data to version block */
	memcpy(ver_bh->b_data, data, JAGUAR_BLOCK_SIZE);
	mark_buffer_dirty(ver_bh);
	brelse(ver_bh);

	DBG("backed up inum %d logical block %d to version block %d\n", 
		(int)i->i_ino, logical_block, ver_block);

	/* update version metadata entry */
	ji = (struct jaguar_inode *) i->i_private;
	jvm = (struct jaguar_version_metadata *) ji->ver_meta_bh->b_data;
	jvme = &jvm->entry[jvm->num_entries];
	jvme->logical_block = logical_block;
	jvme->version_block = ver_block;
	do_gettimeofday(&tv);
	jvme->timestamp = tv.tv_sec;
	jvm->num_entries++;
	DBG("added version entry [%d,%d,%d], num_entries=%d\n", 
		logical_block, ver_block, (int)tv.tv_sec, jvm->num_entries);

	if (jvm->num_entries == VERSION_METADATA_MAX_ENTRIES) {
		mark_buffer_dirty(ji->ver_meta_bh);
		brelse(ji->ver_meta_bh);
		if (alloc_version_meta_block(i) < 0) {
			ERR("error allocating version metadata block\n");
			goto fail;
		}
	}

fail:
	return;
}

static int jaguar_get_block(struct inode *i, sector_t logical_block,
		struct buffer_head *bh, int create)
{
	int block, ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("jaguar_get_block: entering. inum=%d, block=%d, create=%d\n", (int)i->i_ino, (int)logical_block, create);

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;

	/* convert logical block to physical block.
	 */
	block = logical_to_phys_block(i, (int)logical_block);

	if (!block) {
		/* no block was allocated for this offset.
		 * allocate one now 
		 */
		block = alloc_data_block(i->i_sb);
		if (block < 0) {
			ERR("could not allocate data block\n");
			ret = -ENOMEM;
			goto fail;
		}

		/* store the block in the inode */
		update_inode_block_map(i, logical_block, block);

		set_buffer_new(bh);

	} else {
		/* this block was already allocated and probably has data.
		 * if get_block is called for a write and the file is versioned, 
		 * it has to be backed up.
		 */
		//if (create && jid->version_type == JAGUAR_KEEP_ALL) {
		//	version(i, (int)logical_block, block);
		//}
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
	char *buf;

	DBG("jaguar_writepage: entering\n");

	buf = (char *)kmap(page);
	DBG("page virt addr=0x%p, buf[0]=%c, inum=%d\n", buf, buf[0], (int)page->mapping->host->i_ino);
	kunmap((void *)buf);

	return block_write_full_page(page, jaguar_get_block, wbc);
}

static int jaguar_write_begin(struct file *filp, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, 
		struct page **pagep, void **fsdata)
{
	int ret, logical_block;
	char *buf;
	struct inode *i = filp->f_dentry->d_inode;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("jaguar_write_begin: entering, pos=%d, len=%d\n", (int)pos, len);

	ret = block_write_begin(mapping, pos, len, 
			flags, pagep, jaguar_get_block);

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;

	if (jid->version_type == JAGUAR_KEEP_ALL) {
		logical_block = pos / JAGUAR_BLOCK_SIZE;
		buf = (char *)kmap(*pagep);
		DBG("page virt addr=0x%p, buf[0]=%c, inum=%d\n", buf, buf[0], (int)(*pagep)->mapping->host->i_ino);
		version(i, logical_block, buf);
		kunmap((void *)buf);
	}

	return ret;
}


static int jaguar_open(struct inode *i, struct file *f)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;
	struct super_block *sb;

	sb = i->i_sb;
	ji = (struct jaguar_inode *) i->i_private;
	jid = (struct jaguar_inode_on_disk *) &ji->disk_copy;

	DBG("entering jaguar_open: inum=%d\n", (int)i->i_ino);

	if (jid->version_type != 0 && ji->ver_meta_bh == NULL) {
		/* read the version meta block into a buffer */
		if ((ji->ver_meta_bh = __bread(sb->s_bdev, jid->ver_meta_block, JAGUAR_BLOCK_SIZE)) == NULL) {
			ERR("could not read version meta block\n");
			return -ENOMEM;
			goto fail;
		}
	}

	//DBG("opening file %s, parent=%s\n", f->f_path.dentry->d_name.name, f->f_path.dentry->d_parent->d_name.name);
	//DBG("mntroot=%s\n", f->f_path.mnt->mnt_root->d_name.name);

fail:
	return ret;
}

static int jaguar_release(struct inode *i, struct file *f)
{
	struct jaguar_inode *ji = (struct jaguar_inode *) i->i_private;

	DBG("entering jaguar_release: inum=%d\n", (int)i->i_ino);

	if (ji->ver_meta_bh) {
		mark_buffer_dirty(ji->ver_meta_bh);
		brelse(ji->ver_meta_bh);
		ji->ver_meta_bh = NULL;
	}

	return 0;
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
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= jaguar_ioctl,
	.open		= jaguar_open,
	.release	= jaguar_release
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
	if (read_inode_from_disk(i)) {
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

