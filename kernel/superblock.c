#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "jaguar.h"
#include "debug.h"

int jaguar_sb_read(struct super_block *sb)
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

	/* set block size of super block AND the backing block dev */
	if (!sb_set_blocksize(sb, JAGUAR_BLOCK_SIZE)) {
		ERR("error setting block size\n");
		ret = -EIO;
		goto fail;
	}

	/* read the super block from the disk */
	if ((bh = __bread(sb->s_bdev, 0, JAGUAR_BLOCK_SIZE)) == NULL) {
		ERR("error reading super block from disk\n");
		ret = -EIO;
		goto fail;
	}

	memcpy(&jsb->disk_copy, bh->b_data, sizeof(jsb->disk_copy));

	DBG("on disk: sb->name=%s\n", jsb->disk_copy.name);

	brelse(bh);

	return 0;

fail:
	if (jsb)
		kfree(jsb);

	return ret;
}
