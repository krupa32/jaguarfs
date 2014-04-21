#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mount.h>
#include <linux/highmem.h>
#include <asm/uaccess.h>
#include "jaguar.h"
#include "debug.h"

/* statistics variables. defined in ioctl.c */
extern int num_version_calls;

int fill_inode(struct inode *i);
long jaguar_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static void version(struct file *filp, struct inode *i, int logical_block, int phys_block);
static int jaguar_open(struct inode *i, struct file *f);
static int jaguar_release(struct inode *i, struct file *f);

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

	ret = pos + size;
	if ((pos + size) > i->i_size)
		ret = i->i_size - pos;

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

	/* if dir inode is versioned, then take a copy before modifying */
	if (jid->version_type != 0) {
		/* note: when a file/dir is created, the parent dir is
		 * NOT opened, and hence the ver_meta_bh is never loaded.
		 * so we manually do a open, release to make sure the
		 * ver_meta_bh is loaded.
		 */
		jaguar_open(i, NULL);
		version(NULL, i, logical_block, block);
		jaguar_release(i, NULL);
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
		if (read_inode_data(i, (int)filp->f_pos, sizeof(jd), &jd) < 0) {
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
	struct jaguar_inode *ji, *ji_parent;
	struct jaguar_inode_on_disk *jid, *jid_parent;
	struct version_info vinfo;

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

	/* if parent dir was versioned, set it on child too */
	ji_parent = (struct jaguar_inode *) parent->i_private;
	jid_parent = &ji_parent->disk_copy;
	if (jid_parent->version_type != 0) {
		vinfo.type = jid_parent->version_type;
		vinfo.param = jid_parent->version_param;
		set_version(i, &vinfo);
	}
	
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
		if (read_inode_data(parent, pos, sizeof(jd), &jd) < 0) {
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
	struct jaguar_inode_on_disk *jid = &ji->disk_copy;
	int max_blks_at_level[] = { 12, 1024, 1048576 };

	n_blocks = (inode->i_size + JAGUAR_BLOCK_SIZE - 1 ) / JAGUAR_BLOCK_SIZE;
	DBG("free_all_data_blocks: freeing %d blocks\n", n_blocks);

	/* if file/dir is versioned, then free the ver meta block.
	 * actually, there might be more ver meta blocks linked together,
	 * but they are not freed as of now. an easy way to do it would
	 * be to set version type as keep_safe_versions and version_param
	 * as 0, and then call prune().
	 * but for now, only the latest ver meta block is freed.
	 */
	if (jid->version_type != 0 && jid->ver_meta_block != 0)
		free_data_block(inode->i_sb, jid->ver_meta_block);

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
		if (read_inode_data(parent, pos, sizeof(jd), &jd) < 0) {
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
		if (read_inode_data(parent, pos, sizeof(jd), &jd) < 0) {
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

	/* ver_meta_bh is not brelsed here.
	 * it would be eventually released in jaguar_close().
	 */

	DBG("initialized version meta buffer on disk\n");

fail:
	return ret;
}

/*
 * filp			: valid only when files are versioned
 * i			: valid always
 * logical_block	: valid for files and dirs
 * phys_block		: valid only for dirs
 */
static void version(struct file *filp, struct inode *i, int logical_block, int phys_block)
{
	struct buffer_head *ver_bh, *bh = NULL;
	struct super_block *sb;
	struct jaguar_version_metadata *jvm = NULL;
	struct jaguar_version_metadata_entry *jvme, *last_jvme;
	struct jaguar_inode *ji;
	int ver_block, ret;
	struct timeval tv;
	char *data;
	loff_t readpos;
	mm_segment_t oldfs;

	DBG("version: entering: inum=%d, logical=%d\n",
		(int)i->i_ino, logical_block);

	num_version_calls++;

	sb = i->i_sb;

	ji = (struct jaguar_inode *) i->i_private;
	if (ji->ver_meta_bh == NULL) {
		DBG("error: ver_meta_bh is NULL\n");
		return;
	}
	jvm = (struct jaguar_version_metadata *) ji->ver_meta_bh->b_data;

	/* throttle versioning rate.
	 * one entry per second is more than adequate.
	 * if last versioning entry was also added at the same second,
	 * do not version this change. just return.
	 */
	do_gettimeofday(&tv);
	if (jvm->num_entries > 0) {
		last_jvme = &jvm->entry[jvm->num_entries - 1];
		if (last_jvme->timestamp == tv.tv_sec)
			return;
	}

	/* read the old data that is to be versioned. 2 cases here:
	 * 1) for files: filp is valid, and old data is read from filp
	 *    using logical_block
	 * 2) for dirs: filp is NULL, and old data is directly read from bdev
	 *    using phys_block
	 */
	if (filp) {
		readpos = logical_block * JAGUAR_BLOCK_SIZE;
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		ret = do_sync_read(filp, ji->ver_data_buf, JAGUAR_BLOCK_SIZE, &readpos);
		set_fs(oldfs);
		DBG("do_sync_read returned %d, buf[0]=%c\n", ret, ji->ver_data_buf[0]);
		if (ret <= 0)
			goto fail;
		data = ji->ver_data_buf;
	} else {
		if ((bh = __bread(i->i_sb->s_bdev, phys_block, JAGUAR_BLOCK_SIZE)) == NULL) {
			ERR("error reading inode from disk\n");
			goto fail;
		}
		data = bh->b_data;
	}

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
	jvme = &jvm->entry[jvm->num_entries];
	jvme->logical_block = logical_block;
	jvme->version_block = ver_block;
	if (i->i_size < (logical_block+1)*JAGUAR_BLOCK_SIZE)
		jvme->bytes_valid = i->i_size - (logical_block * JAGUAR_BLOCK_SIZE);
	else
		jvme->bytes_valid = JAGUAR_BLOCK_SIZE;
	jvme->timestamp = tv.tv_sec;
	jvm->num_entries++;
	DBG("added version entry [%d,%d,%d,%d], num_entries=%d\n", 
		logical_block, ver_block, jvme->bytes_valid, (int)tv.tv_sec, jvm->num_entries);

	/* if all meta entries are exhausted, write out this ver meta block
	 * and allocate a new one.
	 */
	if (jvm->num_entries == VERSION_METADATA_MAX_ENTRIES) {
		mark_buffer_dirty(ji->ver_meta_bh);
		brelse(ji->ver_meta_bh);
		if (alloc_version_meta_block(i) < 0) {
			ERR("error allocating version metadata block\n");
			goto fail;
		}
	}

fail:
	if (bh)
		brelse(bh);
	return;
}

int retrieve(struct file *filp, int logical_block, int at, char __user *data)
{
	struct jaguar_version_metadata *jvm = NULL;
	struct jaguar_version_metadata_entry *jvme;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;
	struct buffer_head *ver_meta_bh, *bh;
	int ver_block = 0, done = 0, j, next_block = 0, ret, size = 0;
	struct inode *i;
	struct super_block *sb;
	loff_t pos;
	char *kdata;

	i = filp->f_dentry->d_inode;
	sb = i->i_sb;
	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	ver_meta_bh = ji->ver_meta_bh;

	DBG("retrieve: inum=%d, logical=%d, at=%d\n", (int)i->i_ino, logical_block, at);

	if (jid->version_type == 0)
		return -EINVAL;

	while (!done) {

		jvm = (struct jaguar_version_metadata *) ver_meta_bh->b_data;
		jvme = &jvm->entry[jvm->num_entries - 1];

		/* go through current meta data block */
		for (j = jvm->num_entries - 1; j >= jvm->start_entry; j--) {

			DBG("checking version entry logical=%d, ts=%d\n", jvme->logical_block, jvme->timestamp);

			if (jvme->logical_block != logical_block)
				continue;

			if (jvme->timestamp < at) {
				/* found a version timestamp less than 'at'.
				 * the version block corresponding to least
				 * timestamp greater than 'at' is stored in
				 * ver_block.
				 */
				done = 1;
				break;
			} else {
				/* this timestamp is greater than 'at'.
				 * so track it.
				 */
				ver_block = jvme->version_block;
				size = jvme->bytes_valid;
			}

			jvme--;
		}

		next_block = jvm->next_block;

		/* brelse the ver_meta_bh. but ji->ver_meta_bh which was 
		 * already existing, and being used, need not be brelse'ed.
		 */
		if (ver_meta_bh && ver_meta_bh != ji->ver_meta_bh) {
			brelse(ver_meta_bh);
			ver_meta_bh = NULL;
		}

		if (!done) {
			/* no suitable version entry was found in
			 * current block, so check the next_block.
			 */
			if (next_block) {
				/* read the next version meta block into a buffer */
				DBG("reading next version meta block %d\n", next_block);
				if ((ver_meta_bh = __bread(sb->s_bdev, next_block, JAGUAR_BLOCK_SIZE)) == NULL) {
					ERR("could not read version meta block\n");
					goto fail;
				}
			} else {
				/* no more version meta blocks to look */
				DBG("no more version meta blocks\n");
				done = 1;
			}
		}
	}

	if (ver_block == 0) {
		/* no version block was found.
		 * return the latest data from the file.
		 * since the latest data might be on page cache,
		 * and NOT updated on disk, we MUST use vfs_read.
		 */
		DBG("no version data block, returning latest data\n");
		pos = logical_block * JAGUAR_BLOCK_SIZE;
		if (jid->type == INODE_TYPE_FILE) {
			return vfs_read(filp, data, JAGUAR_BLOCK_SIZE, &pos);
		} else {
			kdata = kmalloc(JAGUAR_BLOCK_SIZE, GFP_KERNEL);
			ret = read_inode_data(i, (int)pos, JAGUAR_BLOCK_SIZE, kdata);
			__copy_to_user(data, kdata, JAGUAR_BLOCK_SIZE);
			kfree(kdata);
			return ret;
		}
	}

	DBG("found version data block %d, size=%d\n", ver_block, size);

	/* a proper version block was found. read it and copy the contents
	 * into the user space buffer passed.
	 */
	if ((bh = __bread(sb->s_bdev, ver_block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("could not read version data block\n");
		goto fail;
	}

	__copy_to_user(data, bh->b_data, size);

	brelse(bh);

	return size;

fail:
	return -ENOENT;
}


int prune(struct file *filp)
{
	struct jaguar_version_metadata *jvm = NULL;
	struct jaguar_version_metadata_entry *jvme;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;
	struct buffer_head *ver_meta_bh;
	int done = 0, j, pruning = 0, now, num_versions = 0, start_entry;
	int cur_meta_block, next_meta_block, free_meta_block = 0;
	struct inode *i;
	struct super_block *sb;
	struct timeval tv;

	i = filp->f_dentry->d_inode;
	sb = i->i_sb;
	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	ver_meta_bh = ji->ver_meta_bh;
	do_gettimeofday(&tv);
	now = tv.tv_sec;

	DBG("prune: entering inum=%d, ver type=%d, param=%d\n", (int)i->i_ino, jid->version_type, jid->version_param);

	if (jid->version_type == 0)
		return -EINVAL;
	
	if (jid->version_type == JAGUAR_KEEP_ALL)
		return 0;

	while (!done) {

		jvm = (struct jaguar_version_metadata *) ver_meta_bh->b_data;
		jvme = &jvm->entry[jvm->num_entries - 1];

		/* go through current meta data block */
		start_entry = jvm->start_entry;
		for (j = jvm->num_entries - 1; j >= start_entry; j--) {

			DBG("scanning version %d ts=%d\n", num_versions, jvme->timestamp);
			if (jid->version_type == JAGUAR_KEEP_SAFE_VERSIONS &&
			    num_versions < jid->version_param) {

				/* this entry should not be pruned */
				num_versions++;

			} else if (jid->version_type == JAGUAR_KEEP_SAFE_TIME &&
				   jvme->timestamp >= (now - jid->version_param)) {

				/* this entry should not be pruned */

			} else {
				DBG("pruning entry\n");
				/* this entry should be pruned.
				 * free the version data block
				 */
				free_data_block(sb, jvme->version_block);
				
				/* update the start entry for this version
				 * block. note that this should happen only
				 * once, so a flag is used.
				 */
				if (!pruning) {
					jvm->start_entry = j + 1;
					mark_buffer_dirty(ver_meta_bh);
					pruning = 1;
					DBG("start entry updated to %d\n", j+1);
				}
			}

			jvme--;

		}

		next_meta_block = jvm->next_block;

		/* if pruning, all entries in this meta block are
		 * pruned. set next_block as 0, and continue pruning
		 */
		if (pruning && jvm->next_block) {
			jvm->next_block = 0;
			mark_buffer_dirty(ver_meta_bh);
		}
		
		/* brelse the ver_meta_bh. but ji->ver_meta_bh which was 
		 * already existing, and being used, need not be brelse'ed.
		 */
		if (ver_meta_bh && ver_meta_bh != ji->ver_meta_bh) {
			brelse(ver_meta_bh);
			ver_meta_bh = NULL;
		}

		if (free_meta_block) {
			DBG("freeing meta block %d\n", cur_meta_block);
			free_data_block(sb, cur_meta_block);
		}

		if (next_meta_block) {

			cur_meta_block = next_meta_block;

			/* if already in pruning mode, and moving to next
			 * block, then we can start freeing the ver meta
			 * block from next iteration.
			 */
			if (pruning)
				free_meta_block = 1;

			/* read the next version meta block into a buffer */
			DBG("reading next version meta block %d\n", cur_meta_block);
			if ((ver_meta_bh = __bread(sb->s_bdev, cur_meta_block, JAGUAR_BLOCK_SIZE)) == NULL) {
				ERR("could not read version meta block\n");
				goto fail;
			}
		} else {
			/* no more version meta blocks to look */
			DBG("no more version meta blocks\n");
			done = 1;
		}

	}

fail:
	return 0;
}


/* Note: Rolling back dir is currently just overwriting the directory
 * contents on disk. It DOES NOT invalidate the dentries already cached
 * by VFS. So, even though 'ls' would display the latest dir entries,
 * older (non-existing) file names may still be resolved. However, this
 * should not happen after a umount/mount.
 */
int rollback_dir(struct inode *i, int offset, int nbytes, char __user *data)
{
	int ret = 0;
	void *buf;

	DBG("rollback_dir: entering, offset=%d, nbytes=%d\n", offset, nbytes);

	if ((buf = kmalloc(JAGUAR_BLOCK_SIZE, GFP_KERNEL)) == NULL) {
		ERR("could not allocate mem\n");
		return -ENOMEM;
	}

	__copy_from_user(buf, data, JAGUAR_BLOCK_SIZE);

	ret = write_inode_data(i, offset, nbytes, buf);

	i->i_size = offset + nbytes;
	mark_inode_dirty(i);

	kfree(buf);

	return ret;
}

static int jaguar_get_block(struct inode *i, sector_t logical_block,
		struct buffer_head *bh, int create)
{
	int block, ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("jaguar_get_block: entering. inum=%d, block=%d, create=%d\n", 
		(int)i->i_ino, (int)logical_block, create);

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

static int jaguar_write_begin(struct file *filp, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags, 
		struct page **pagep, void **fsdata)
{
	DBG("jaguar_write_begin: entering, pos=%d, len=%d\n", (int)pos, len);

	return block_write_begin(mapping, pos, len, 
			flags, pagep, jaguar_get_block);
}

static int jaguar_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{

	DBG("jaguar_write_end: entering, pos=%d, len=%d, copied=%d\n", (int)pos, len, copied);

	return generic_write_end(file, mapping, pos, len, copied, page, fsdata);
}

static int jaguar_open(struct inode *i, struct file *filp)
{
	int ret = 0, logical_block;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;
	struct super_block *sb;
	loff_t readpos;
	mm_segment_t oldfs;

	sb = i->i_sb;
	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;

	DBG("entering jaguar_open: inum=%d\n", (int)i->i_ino);

	if (jid->version_type != 0) {
		if (ji->ver_meta_bh == NULL) {
			/* read the version meta block into a buffer */
			if ((ji->ver_meta_bh = __bread(sb->s_bdev, jid->ver_meta_block, JAGUAR_BLOCK_SIZE)) == NULL) {
				ERR("could not read version meta block\n");
				ret = -ENOMEM;
				goto fail;
			}
		}

		if (ji->ver_data_buf == NULL) {
			/* allocate a buffer to hold data that is to be versioned */
			if ((ji->ver_data_buf = kmalloc(JAGUAR_BLOCK_SIZE, GFP_KERNEL)) == NULL) {
				ERR("could not allocate version data buffer\n");
				ret = -ENOMEM;
				goto fail;
			}
		}
	}

	/* IMPORTANT: if O_TRUNC is set, then all page cache pages for this
	 * file are freed immediately after the file is opened. the file's
	 * data should be backed up in jaguar_open() itself.
	 */
	if (jid->version_type != 0 && filp && (filp->f_flags & O_TRUNC)) {
		DBG("O_TRUNC is set, backing up all file data\n");
		logical_block = 0;
		readpos = 0;
		while (1) {
			oldfs = get_fs();
			set_fs(KERNEL_DS);
			ret = do_sync_read(filp, ji->ver_data_buf, JAGUAR_BLOCK_SIZE, &readpos);
			set_fs(oldfs);
			if (ret > 0)
				version(filp, i, logical_block, 0);
			if (ret < JAGUAR_BLOCK_SIZE)
				break;
			logical_block++;
		}
	}

	return 0;

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

	if (ji->ver_data_buf) {
		kfree(ji->ver_data_buf);
		ji->ver_data_buf = NULL;
	}

	return 0;
}

ssize_t jaguar_write(struct file *filp, const char __user *buf, size_t len, loff_t *pos)
{
	int logical_block, remaining = len;
	struct inode *i;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("jaguar_write: entering pos=%d, len=%d\n", (int)*pos, len);

	i = filp->f_dentry->d_inode;
	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;

	/* IMPORTANT: if O_TRUNC is set, then all page cache pages for this
	 * file are freed immediately after the file is opened. the file's
	 * data would have been backed up in jaguar_open() itself.
	 * also, after freeing the pages, VFS resets the O_TRUNC flag.
	 * so when it comes here, only O_WRONLY flag is set.
	 * the only way to handle this case is to check the return value
	 * of do_sync_read(). if it is 0, then file was truncated.
	 */
	if (jid->version_type != 0) {
		logical_block = *pos / JAGUAR_BLOCK_SIZE;
		while (remaining > 0) {
			version(filp, i, logical_block, 0);
			remaining -= JAGUAR_BLOCK_SIZE;
			logical_block++;
		}
	}

	return do_sync_write(filp, buf, len, pos);
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
	.write		= jaguar_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.llseek		= generic_file_llseek,
	.unlocked_ioctl	= jaguar_ioctl,
	.open		= jaguar_open,
	.release	= jaguar_release,
	.fsync		= generic_file_fsync
};

static const struct address_space_operations jaguar_aops = {
	.readpage	= jaguar_readpage,
	.writepage	= jaguar_writepage,
	.write_begin	= jaguar_write_begin,
	.write_end	= jaguar_write_end
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


int set_version(struct inode *i, struct version_info *info)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("set_version: entering, inum=%d\n", (int)i->i_ino);

	/* mark the inode as versioned */
	ji = (struct jaguar_inode *) i->i_private;
	ji->ver_meta_bh = NULL;
	jid = &ji->disk_copy;
	jid->version_type = info->type;
	jid->version_param = info->param;

	if (jid->ver_meta_block == 0) {
		if ((ret = alloc_version_meta_block(i)) < 0) {
			ERR("error allocating version meta block\n");
			goto fail;
		}
	}

	mark_inode_dirty(i);

fail:
	return ret;
}

int reset_version(struct inode *i)
{
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("reset_version: entering, inum=%d\n", (int)i->i_ino);

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	jid->version_type = 0;
	jid->version_param = 0;
	jid->ver_meta_block = 0;
	mark_inode_dirty(i);

	return 0;
}
