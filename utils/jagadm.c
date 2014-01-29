#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include "jaguar.h"

#define ACTION_VERSION		1
#define ACTION_UNVERSION	2

static void usage(void)
{
	printf("Usage: jagadm -a ACTION FILE/DIR\n"
		"ACTION can be\n"
		"version        - Version the FILE/DIR\n"
		"unversion      - Unversion the FILE/DIR\n"
		);
}

static int version(const char *filename)
{
	int fd = -1, ret = -EINVAL;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

	if ((ret = ioctl(fd, JAGUAR_IOC_VERSION, NULL)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

err:
	if (fd > 0)
		close(fd);

	return ret;
}

static int unversion(const char *filename)
{
	int fd = -1, ret = -EINVAL;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

	if ((ret = ioctl(fd, JAGUAR_IOC_UNVERSION, NULL)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

err:
	if (fd > 0)
		close(fd);

	return ret;
}


int main(int argc, char **argv)
{
	int opt, action, ret = -EINVAL;

	while ((opt = getopt(argc, argv, "a:")) != -1) {
		switch (opt) {
		case 'a':
			if (strcmp(optarg, "version") == 0) {
				action = ACTION_VERSION;
			} else if (strcmp(optarg, "unversion") == 0) {
				action = ACTION_UNVERSION;
			} else {
				usage();
				exit(1);
			}
			break;
		default: /* -? */
			usage();
			exit(1);
		}
	}

	switch (action) {
	case ACTION_VERSION:
		ret = version(argv[optind]);
		break;
	case ACTION_UNVERSION:
		ret = unversion(argv[optind]);
		break;
	}

	return ret;
}
