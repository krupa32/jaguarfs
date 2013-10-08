#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mount.h>
#include "jaguar.h"
#include "debug.h"

static struct dentry *jaguar_mount(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data)
{
	DBG("jaguar_mount: entering\n");
	return mount_bdev(fs_type, flags, dev_name, data, jaguar_fill_super);
}



static struct file_system_type jaguar_fs_type = {
	.owner 		= THIS_MODULE,
	.name 		= "jaguarfs",
	.mount 		= jaguar_mount,
	.kill_sb 	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV
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

