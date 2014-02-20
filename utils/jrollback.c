#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "jaguar.h"

static int jrollback(const char *filename, time_t at)
{
	int fd, done = 0, nbytes;
	struct version_buffer ver_buf;

	if ((fd = open(filename, O_WRONLY|O_TRUNC)) < 0) {
		perror(NULL);
		return errno;
	}

	memset(&ver_buf, 0, sizeof(ver_buf));
	ver_buf.at = at;

	while (!done) {

		if ((nbytes = ioctl(fd, JAGUAR_IOC_RETRIEVE, &ver_buf)) < 0) {
			perror(NULL);
			return errno;
		}

		ver_buf.data[nbytes] = 0;
		//printf("restoring offset=%d, nbytes=%d\n", ver_buf.offset, nbytes);

		if (write(fd, ver_buf.data, nbytes) < 0) {
			printf("error restoring\n");
			done = 1;
		}

		if (nbytes < JAGUAR_BLOCK_SIZE)
			done = 1;

		ver_buf.offset += JAGUAR_BLOCK_SIZE;
		memset(ver_buf.data, 0, JAGUAR_BLOCK_SIZE);
	}

	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	struct tm tm_arg;
	time_t time_arg;

	if (argc != 3) {
		printf("Usage: jrollback FILE/DIR TIME\n"
			"FILE/DIR - file name or directory name\n"
			"TIME - time in the format DD-MM-YYYY:HH:MM:SS\n");
		return -1;
	}

	strptime(argv[2], "%d-%m-%Y:%H:%M:%S", &tm_arg);
	time_arg = mktime(&tm_arg);
	//printf("now=%d, time_arg=%d\n", (int)time(NULL), (int)time_arg);

	return jrollback(argv[1], time_arg);
}
