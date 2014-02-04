#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include "jaguar.h"

#define ACTION_VERSION		1
#define ACTION_UNVERSION	2
#define ACTION_PRUNE		3

static void usage(void)
{
	printf("Usage: jagadm -a ACTION [-t TYPE] [-p PARAM] FILE/DIR\n"
		"ACTION can be\n"
		"version        - Version the FILE/DIR\n"
		"unversion      - Unversion the FILE/DIR\n"
		"prune		- Remove the older versions that are not needed\n"
		"TYPE can be\n"
		"all		- Keep all older versions\n"
		"time		- Keep versions within last few seconds\n"
		"number		- Keep last few versions\n"
		"PARAM can be\n"
		"number of seconds (when TYPE is time) or\n"
		"number of versions (when TYPE is number)\n"
		);
}

static int version(const char *filename, int type, int param)
{
	int fd = -1, ret = -EINVAL;
	struct version_info info;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

	info.type = type;
	info.param = param;

	if ((ret = ioctl(fd, JAGUAR_IOC_VERSION, &info)) < 0) {
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

static int prune(const char *filename)
{
	int fd = -1, ret = -EINVAL;

	if ((fd = open(filename, O_RDONLY)) < 0) {
		ret = errno;
		perror(NULL);
		goto err;
	}

	if ((ret = ioctl(fd, JAGUAR_IOC_PRUNE, NULL)) < 0) {
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
	int type = 0, param = 0;

	while ((opt = getopt(argc, argv, "a:t:p:")) != -1) {
		switch (opt) {
		case 'a':
			if (strcmp(optarg, "version") == 0) {
				action = ACTION_VERSION;
			} else if (strcmp(optarg, "unversion") == 0) {
				action = ACTION_UNVERSION;
			} else if (strcmp(optarg, "prune") == 0) {
				action = ACTION_PRUNE;
			} else {
				usage();
				exit(1);
			}
			break;
		case 't':
			if (strcmp(optarg, "all") == 0) {
				type = JAGUAR_KEEP_ALL;
			} else if (strcmp(optarg, "time") == 0) {
				type = JAGUAR_KEEP_SAFE_TIME;
			} else if (strcmp(optarg, "number") == 0) {
				type = JAGUAR_KEEP_SAFE_VERSIONS;
			} else {
				usage();
				exit(1);
			}
			break;
		case 'p':
			param = strtol(optarg, NULL, 10);
			break;
		default: /* -? */
			usage();
			exit(1);
		}
	}

	switch (action) {
	case ACTION_VERSION:
		ret = version(argv[optind], type, param);
		break;
	case ACTION_UNVERSION:
		ret = unversion(argv[optind]);
		break;
	case ACTION_PRUNE:
		ret = prune(argv[optind]);
		break;
	}

	return ret;
}
