#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mount.h>
#include "jaguar.h"
#include "debug.h"


/* Supposed to fill the super block related information.
 * Most important info is the s_root field, which points to
 * the inode of the root directory, along with its inode_ops and f_ops.
 */
static int jaguar_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret = -EINVAL;
	struct inode *root_inode = NULL;

	DBG("jaguar_fill_super: entering\n");

	/* read the super block from disk */
	if (jaguar_sb_read(sb)) {
		ERR("error reading super block from disk\n");
		ret = -EIO;
		goto fail;
	}

	/* create the root dir inode of the fs */
	root_inode = jaguar_iget(sb, 1);
	if (IS_ERR(root_inode)) {
		ERR("error allocating root inode\n");
		ret = PTR_ERR(root_inode);
		goto fail;
	}

	set_nlink(root_inode, 2);

	/* setup the dentry of the root dir in super block */
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ERR("d_make_root failed\n");
		goto fail;
	}

	return 0;

fail:

	return ret;
}

static struct dentry *jaguar_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	DBG("jaguar_mount: entering\n");
	return mount_bdev(fs_type, flags, dev_name, data, jaguar_fill_super);
}



static struct file_system_type jaguar_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"jaguarfs",
	.mount =	jaguar_mount,
	.kill_sb =	kill_block_super,
};



static int __init jaguar_init(void)
{
	int retval;

	DBG("jaguar_init: entering\n");

	retval = register_filesystem(&jaguar_fs_type);

	return retval;
}

static void __exit jaguar_exit(void)
{
	DBG("jaguar_exit: entering\n");

	unregister_filesystem(&jaguar_fs_type);

	DBG("jaguar_exit: exiting\n");
}
module_init(jaguar_init);
module_exit(jaguar_exit);

