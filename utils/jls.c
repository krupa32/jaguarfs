#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "jaguar.h"

#define JAGUAR_FILENAME_MAX		60
struct jaguar_dentry
{
	unsigned int inum;
	char name[JAGUAR_FILENAME_MAX];
};


static int jls(const char *dirname, time_t at)
{
	int fd, done = 0, nbytes, i;
	struct version_buffer ver_buf;
	struct jaguar_dentry *dentry;

	if ((fd = open(dirname, O_RDONLY)) < 0) {
		perror(NULL);
		return errno;
	}

	memset(&ver_buf, 0, sizeof(ver_buf));
	ver_buf.at = at;

	while (!done) {

		if ((nbytes = ioctl(fd, JAGUAR_IOC_RETRIEVE, &ver_buf)) < 0) {
			/* reading beyond end of directory.
			 * ignore it. further down, it will anyway set
			 * 'done' and exist graciously.
			 */
		}

		for (i = 0; i < nbytes; i += sizeof(*dentry)) {
			dentry = (struct jaguar_dentry *) (ver_buf.data + i);
			if (dentry->inum != 0)
				printf("%s\n", dentry->name);
		}

		if (nbytes < JAGUAR_BLOCK_SIZE)
			done = 1;

		ver_buf.offset += JAGUAR_BLOCK_SIZE;
		memset(ver_buf.data, 0, JAGUAR_BLOCK_SIZE);
	}

	close(fd);

}

int main(int argc, char **argv)
{
	struct tm tm_arg;
	time_t time_arg;

	if (argc != 3) {
		printf("Usage: jls DIR TIME\n"
			"DIR - dirname\n"
			"TIME - time in the format DD-MM-YYYY:HH:MM:SS\n");
		return -1;
	}

	strptime(argv[2], "%d-%m-%Y:%H:%M:%S", &tm_arg);
	time_arg = mktime(&tm_arg);
	//printf("now=%d, time_arg=%d\n", (int)time(NULL), (int)time_arg);

	return jls(argv[1], time_arg);
}
