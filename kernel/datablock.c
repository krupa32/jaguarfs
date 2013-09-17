#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "jaguar.h"
#include "debug.h"

int alloc_data_block(struct super_block *sb)
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
	 * the next free data block, which can be used as a hint to start reading
	 * from the corresponding page, and then iterate through the pages
	 * of the bmap.
	 *
	 * OR another idea is to forget about any optimization, and ALWAYS
	 * read from the start of the bmap, iterate through the pages and
	 * find a free inode.
	 */

	/* read data bitmap from disk */
	block = BYTES_TO_BLOCK(jsbd->data_bmap_start);
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading data bmap from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* find the first zero bit, set it to 1 */
	ret = (int)jaguar_find_first_zero_bit(bh->b_data, JAGUAR_BLOCK_SIZE);
	jaguar_set_bit(bh->b_data, ret);
	mark_buffer_dirty(bh);
	brelse(bh);
	DBG("alloc_data_block: found blk %d\n", ret);

	/* zero out the allocated block */
	if ((bh = __bread(sb->s_bdev, ret, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading data block from disk\n");
		ret = -EIO;
		goto fail;
	}
	memset(bh->b_data, 0, JAGUAR_BLOCK_SIZE);
	mark_buffer_dirty(bh);

fail:
	if (bh)
		brelse(bh);
	return ret;
}

int free_data_block(struct super_block *sb, int block_to_free)
{
	int ret = 0;
	int block;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct buffer_head *bh = NULL;

	jsb = sb->s_fs_info;
	jsbd = &jsb->disk_copy;

	/*
	 * TODO The following code reads only one page of bmap.
	 * It has to take care of condition where bmap can be of
	 * multiple pages
	 */

	/* read data bitmap from disk */
	block = BYTES_TO_BLOCK(jsbd->data_bmap_start);
	//DBG("free_data_block: reading data bmap from block %d\n", block);
	if ((bh = __bread(sb->s_bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading data bmap from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* find the first zero bit, set it to 1 */
	jaguar_clear_bit(bh->b_data, block_to_free);
	mark_buffer_dirty(bh);
	DBG("free_data_block: freed blk %d\n", block_to_free);

fail:
	if (bh)
		brelse(bh);
	return ret;

}
