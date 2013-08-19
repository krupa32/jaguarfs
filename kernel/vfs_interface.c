#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include "debug.h"

/* Supposed to find the inode corresponding to d->d_name.name, iget the
 * corresponding inode, and map the 'd' to the inode using d_add().
 */
struct dentry *jaguar_lookup(struct inode *dir, struct dentry *d, unsigned int flags)
{
	DBG("jaguar_lookup: entering\n");
	DBG("name=%s\n", d->d_name.name);

	d_add(d, dir);

	DBG("jaguar_lookup: leaving\n");
	return NULL;
}

/* This is called by getdents syscall, which is called by ls.
 * Supposed to read from the filp->f_pos offset of the directory file,
 * and fill in dirent entries using the filldir callback fn.
 * dirent entries are filled as long as filldir returns 0.
 * Note that filp->f_pos has to be updated by this function to reflect
 * how far in the dir we have proceeded.
 * Also, filldir fn ptr is usually compat_filldir.
 */
static int jaguar_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	DBG("jaguar_readdir: entering, pos=%d\n", (int)filp->f_pos);

	if (filp->f_pos == 0) {
		filldir(dirent, "krupa", 5, 0, 1, DT_UNKNOWN);
		filp->f_pos += 5;
	}

	DBG("jaguar_readdir: leaving\n");

	return 0;
}	

static const struct super_operations jaguar_sops = {
	NULL
};

static const struct inode_operations jaguar_dir_inode_ops = {
	.lookup		= jaguar_lookup
};

static const struct file_operations jaguar_dir_file_ops = {
	.readdir	= jaguar_readdir
};

/* Supposed to fill the super block related information.
 * Most important info is the s_root field, which points to
 * the inode of the root directory, along with its inode_ops and f_ops.
 */
static int jaguar_fill_super(struct super_block *sb, void *data, int silent)
{
	int ret = -EINVAL;
	struct inode *root_inode = NULL;

	DBG("jaguar_fill_super: entering\n");

	root_inode = new_inode(sb);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		goto fail;
	}
	root_inode->i_ino = 1;
	root_inode->i_mode = S_IFDIR | 0755;
	root_inode->i_op = &jaguar_dir_inode_ops;
	root_inode->i_fop = &jaguar_dir_file_ops;
	set_nlink(root_inode, 2);

	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ERR("d_make_root failed\n");
		goto fail;
	}

	return 0;

fail:
	DBG("error = %d\n", ret);
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

