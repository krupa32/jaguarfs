#include <linux/fs.h>
#include "jaguar.h"
#include "debug.h"

static int do_version(struct inode *i, void *arg)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("do_version: entering, inum=%d\n", (int)i->i_ino);

	/* mark the inode as versioned */
	ji = (struct jaguar_inode *) i->i_private;
	ji->ver_meta_bh = NULL;
	jid = &ji->disk_copy;
	jid->version_type = JAGUAR_KEEP_ALL;
	jid->version_param = 0;
	jid->ver_meta_block = 0;

	if ((ret = alloc_version_meta_block(i)) < 0) {
		ERR("error allocating version meta block\n");
		goto fail;
	}

	mark_inode_dirty(i);

fail:
	return ret;
}

static int do_unversion(struct inode *i, void *arg)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("do_unversion: entering, inum=%d\n", (int)i->i_ino);

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	jid->version_type = 0;
	mark_inode_dirty(i);

	return ret;
}

long jaguar_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	struct inode *i = filp->f_dentry->d_inode;

	DBG("jaguar_ioctl: entering\n");

	switch (cmd) {
	case JAGUAR_IOC_VERSION:
		ret = do_version(i, (void *)arg);
		break;
	case JAGUAR_IOC_UNVERSION:
		ret = do_unversion(i, (void *)arg);
		break;
	default:
		ret = -ENOTTY;
	}

	return 0;
}
