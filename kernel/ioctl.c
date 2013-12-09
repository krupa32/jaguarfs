#include <linux/fs.h>
#include "jaguar.h"
#include "debug.h"

long jaguar_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	DBG("jaguar_ioctl: entering\n");
	return 0;
}
