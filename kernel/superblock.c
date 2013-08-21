#include <linux/fs.h>
#include <linux/buffer_head.h>
#include "jaguar.h"
#include "debug.h"

int jaguar_sb_read(struct super_block *sb)
{
	struct buffer_head *bh;
	struct jaguar_super_block *jsb = 
		(struct jaguar_super_block *) sb->s_fs_info;

	DBG("jaguar_sb_read: entering\n");

	if ((bh = __bread(sb->s_bdev, 0, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading super block from disk\n");
		return -EIO;
	}

	memcpy(&jsb->disk_copy, bh->b_data, sizeof(jsb->disk_copy));

	brelse(bh);

	return 0;
}
