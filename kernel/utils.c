#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "jaguar.h"
#include "debug.h"

int jaguar_find_first_zero_bit(void *buf, int size)
{
	int ret, i = 0;
	unsigned char mask, *bmap = (unsigned char *)buf;

	/* find 1st byte with a 0 bit */
	while (i < size && *(bmap + i) == 0xFF)
		i++;

	if (i == size)
		return -1;

	ret = i * 8;
	mask = 0x80;
	while ( *(bmap + i) & mask) {
		mask = mask >> 1;
		ret++;
	}

	//DBG("jaguar_find_first_zero_bit: returning %d\n", ret);
	return ret;
}

void jaguar_set_bit(void *buf, int pos)
{
	int byte, bit;
	unsigned char mask, *bmap = (unsigned char *)buf;

	byte = pos / 8;
	bit = pos % 8;
	mask = 0x80;
	while (bit--)
		mask = mask >> 1;

	//DBG("jaguar_set_bit: pos=%d, byte=%d, mask=0x%x\n", pos, byte, mask);
	bmap[byte] |= mask;
}

void jaguar_clear_bit(void *buf, int pos)
{
	int byte, bit;
	unsigned char mask, *bmap = (unsigned char *)buf;

	byte = pos / 8;
	bit = pos % 8;
	mask = 0x80;
	while (bit--)
		mask = mask >> 1;

	//DBG("jaguar_clear_bit: pos=%d, byte=%d, mask=0x%x\n", pos, byte, mask);
	bmap[byte] &= ~mask;
}

/* allocate a bit from a bitmap on disk.
 * bdev:	block dev where the bmap resides
 * bmap_start: 	start block of the bitmap
 * bmap_size:	num blocks the bitmap occupies
 * start:	hint on which block to start from, relative to bmap_start.
 * 		ie, if bmap_start = 10, and start = 1, the function
 * 		would start searching from phys block 11.
 */
int jaguar_bmap_alloc_bit(struct block_device *bdev,
	int bmap_start, int bmap_size, int start)
{
	int block, ret, found = 0, bit;
	struct buffer_head *bh = NULL;

	DBG("jaguar_bmap_alloc_bit: entering bmap_start=%d, bmap_size=%d, start=%d\n",
		bmap_start, bmap_size, start);

	block = start;
	do {
		/* read bitmap from disk */
		if ((bh = __bread(bdev, bmap_start + block, 
				JAGUAR_BLOCK_SIZE)) == NULL) {
			ERR("error reading inode bmap from disk\n");
			ret = -EIO;
			goto fail;
		}

		ret = (int)jaguar_find_first_zero_bit(bh->b_data, JAGUAR_BLOCK_SIZE);
		if (ret >= 0) {
			found = 1;
			break;
		}

		block = (block + 1) % bmap_size;
		DBG("moving to next block %d\n", block);

		brelse(bh);
		bh = NULL;

	} while (block != start);

	if (!found) {
		ERR("could not find free bit in bmap\n");
		ret = -ENOMEM;
		goto fail;
	}

	/* mark the bmap bit as allocated */
	jaguar_set_bit(bh->b_data, ret);
	mark_buffer_dirty(bh);

	/* 'ret' is the first free bit in the _current_ bmap block.
	 * it may not be the bit offset (since current bmap block may not
	 * be the first).
	 */
	bit = block * NUM_BITS_PER_BLOCK + ret;
	DBG("jaguar_bmap_alloc_bit: found bit %d\n", bit);
	ret = bit;

fail:
	if (bh)
		brelse(bh);
	return ret;

}

int jaguar_bmap_free_bit(struct block_device *bdev, int bmap_start, int bit)
{
	int ret = 0, block, offset;
	struct buffer_head *bh = NULL;

	DBG("jaguar_bmap_free_bit: entering bmap_start=%d, bit=%d\n", bmap_start, bit);

	/* read bitmap from disk */
	block = bmap_start + (bit / NUM_BITS_PER_BLOCK);
	if ((bh = __bread(bdev, block, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading inode bmap from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* mark the bit as free in the bmap */
	offset = bit % NUM_BITS_PER_BLOCK;
	jaguar_clear_bit(bh->b_data, offset);
	mark_buffer_dirty(bh);
	DBG("jaguar_bmap_free_bit: freed bit %d\n", bit);

fail:
	if (bh)
		brelse(bh);
	return ret;
}
