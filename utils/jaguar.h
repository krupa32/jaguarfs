#ifndef JAGUAR_H
#define JAGUAR_H

#include <linux/ioctl.h>

#define JAGUAR_BLOCK_SIZE		4096

/* 
 * ioctls
 */
#define JAGUAR_IOC_VERSION		_IOW('f', 100, int)
#define JAGUAR_IOC_UNVERSION		_IO('f', 101)
#define JAGUAR_IOC_RETRIEVE		_IOWR('f', 102, int)
#define JAGUAR_IOC_PRUNE		_IO('f', 103)

/*
 * versioning types
 */
#define JAGUAR_KEEP_ALL			1
#define JAGUAR_KEEP_SAFE_VERSIONS	2
#define JAGUAR_KEEP_SAFE_TIME		3

struct version_info
{
	int type;
	int param;
};

struct version_buffer
{
	int offset;
	int at;
	char data[JAGUAR_BLOCK_SIZE];
};


#endif // JAGUAR_H
