#include <linux/fs.h>
#include <asm/uaccess.h>
#include "jaguar.h"
#include "debug.h"


static int do_version(struct inode *i, struct version_info __user *arg)
{
	struct version_info info;

	__copy_from_user(&info, arg, sizeof(info));

	return set_version(i, &info);
}

static int do_unversion(struct inode *i, void *arg)
{
	return reset_version(i);
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

static int do_rollback_dir(struct inode *i, struct version_buffer __user *ver_buf)
{
	int offset, nbytes;

	__copy_from_user(&offset, &ver_buf->offset, sizeof(int));
	__copy_from_user(&nbytes, &ver_buf->at, sizeof(int));

	return rollback_dir(i, offset, nbytes, ver_buf->data);
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
	case JAGUAR_IOC_ROLLBACK_DIR:
		ret = do_rollback_dir(i, (struct version_buffer *)arg);
		break;
	default:
		ret = -ENOTTY;
	}

	return ret;
}
