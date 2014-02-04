#include <linux/fs.h>
#include <asm/uaccess.h>
#include "jaguar.h"
#include "debug.h"

struct version_buffer 
{
	int offset;
	int at;
	char data[JAGUAR_BLOCK_SIZE];
};

struct version_info
{
	int type;
	int param;
};


static int do_version(struct inode *i, struct version_info __user *arg)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;
	struct version_info info;

	DBG("do_version: entering, inum=%d\n", (int)i->i_ino);

	__copy_from_user(&info, arg, sizeof(info));

	/* mark the inode as versioned */
	ji = (struct jaguar_inode *) i->i_private;
	ji->ver_meta_bh = NULL;
	jid = &ji->disk_copy;
	jid->version_type = info.type;
	jid->version_param = info.param;

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

static int do_unversion(struct inode *i, void *arg)
{
	int ret = 0;
	struct jaguar_inode *ji;
	struct jaguar_inode_on_disk *jid;

	DBG("do_unversion: entering, inum=%d\n", (int)i->i_ino);

	ji = (struct jaguar_inode *) i->i_private;
	jid = &ji->disk_copy;
	jid->version_type = 0;
	jid->version_param = 0;
	jid->ver_meta_block = 0;
	mark_inode_dirty(i);

	return ret;
}

static int do_retrieve(struct file *filp, struct version_buffer __user *ver_buf)
{
	int offset, at, logical_block;

	__copy_from_user(&offset, &ver_buf->offset, sizeof(int));
	__copy_from_user(&at, &ver_buf->at, sizeof(int));

	logical_block = offset / JAGUAR_BLOCK_SIZE;


	return retrieve(filp, logical_block, at, ver_buf->data);

}

static int do_prune(struct file *filp, void *arg)
{
	return prune(filp);
}

long jaguar_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = -ENOTTY;
	struct inode *i = filp->f_dentry->d_inode;

	DBG("jaguar_ioctl: entering\n");

	switch (cmd) {
	case JAGUAR_IOC_VERSION:
		ret = do_version(i, (struct version_info *)arg);
		break;
	case JAGUAR_IOC_UNVERSION:
		ret = do_unversion(i, (void *)arg);
		break;
	case JAGUAR_IOC_RETRIEVE:
		ret = do_retrieve(filp, (struct version_buffer *)arg);
		break;
	case JAGUAR_IOC_PRUNE:
		ret = do_prune(filp, (void *)arg);
		break;
	default:
		ret = -ENOTTY;
	}

	return ret;
}
