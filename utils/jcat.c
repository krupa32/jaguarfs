#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include "jaguar.h"

static int jcat(const char *filename, time_t at)
{
	int fd, done = 0, nbytes;
	struct version_buffer ver_buf;

	if ((fd = open(filename, O_RDONLY)) < 0) {
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

		printf("%s", ver_buf.data);

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
		printf("Usage: jcat FILE TIME\n"
			"FILE - filename\n"
			"TIME - time in the format DD-MM-YYYY:HH:MM:SS\n");
		return -1;
	}

	strptime(argv[2], "%d-%m-%Y:%H:%M:%S", &tm_arg);
	time_arg = mktime(&tm_arg);
	//printf("now=%d, time_arg=%d\n", (int)time(NULL), (int)time_arg);

	return jcat(argv[1], time_arg);
}
