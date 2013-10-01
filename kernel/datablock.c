#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "jaguar.h"
#include "debug.h"

int alloc_data_block(struct super_block *sb)
{
	int ret = -1, blknum, start;
	int bmap_start, bmap_size;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;
	struct buffer_head *bh = NULL;

	jsb = sb->s_fs_info;
	jsbd = jsb->disk_copy;

	if (jsbd->n_blocks_free == 0) {
		ERR("no more blocks available\n");
		ret = -ENOMEM;
		goto fail;
	}

	bmap_start = BYTES_TO_BLOCK(jsbd->data_bmap_start);
	bmap_size = BYTES_TO_BLOCK(jsbd->data_bmap_size);
	start = (jsbd->next_free_block / NUM_BITS_PER_BLOCK);

	/* allocate a data block from the bitmap */
	blknum = jaguar_bmap_alloc_bit(sb->s_bdev, 
			bmap_start, bmap_size, start);
	if (blknum < 0) {
		ERR("could not alloc data block\n");
		ret = -ENOMEM;
		goto fail;
	}
	DBG("alloc_data_block: found blknum %d\n", blknum);

	/* update super block info */
	jsbd->n_blocks_free--;
	jsbd->next_free_block = (blknum + 1) % jsbd->n_blocks;
	mark_buffer_dirty(jsb->bh);

	/* zero out the allocated block */
	if ((bh = __bread(sb->s_bdev, blknum, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading data blk from disk\n");
		ret = -EIO;
		goto fail;
	}
	memset(bh->b_data, 0, JAGUAR_BLOCK_SIZE);
	mark_buffer_dirty(bh);

	ret = blknum;

fail:
	if (bh)
		brelse(bh);
	return ret;
}

int free_data_block(struct super_block *sb, int block_to_free)
{
	int ret = 0;
	int bmap_start;
	struct jaguar_super_block *jsb;
	struct jaguar_super_block_on_disk *jsbd;

	jsb = sb->s_fs_info;
	jsbd = jsb->disk_copy;

	bmap_start = BYTES_TO_BLOCK(jsbd->data_bmap_start);

	/* update the data bitmap */
	ret = jaguar_bmap_free_bit(sb->s_bdev, bmap_start, block_to_free);
	if (ret < 0) {
		ERR("error updating data bitmap\n");
		ret = -EIO;
		goto fail;
	}

	DBG("free_data_block: freed blk %d\n", block_to_free);

	/* update super block info */
	jsbd->n_blocks_free++;
	mark_buffer_dirty(jsb->bh);

fail:
	return ret;

}
