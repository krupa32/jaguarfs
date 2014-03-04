#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_BLOCK_SIZE	4096

#define NUM_ITERATIONS		20000000

int main(int argc, char **argv)
{
	int fd, bs = DEFAULT_BLOCK_SIZE, count, i, time;
	struct timeval tv_before, tv_after;
	float tp;
	char  *buf;

	if (argc > 1)
		bs = strtol(argv[1], NULL, 10);

	count = NUM_ITERATIONS;

	unlink("test.out");

	buf = (char *)malloc(bs);
	memset(buf, 'a', bs);

	printf("starting overwrite test with block size %d, count %d\n", bs, count);

	if ((fd = open("test.out", O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR)) < 0) {
		perror(NULL);
		return -1;
	}

	gettimeofday(&tv_before, NULL);
	printf("before:%d,%d\n", (int)tv_before.tv_sec, (int)tv_before.tv_usec);

	for (i = 0; i < count; i++) {
		lseek(fd, 0, SEEK_SET);
		if (write(fd, buf, bs) < bs) {
			perror(NULL);
			break;
		}
	}

	gettimeofday(&tv_after, NULL);
	printf("after:%d,%d\n", (int)tv_after.tv_sec, (int)tv_after.tv_usec);

	time = (tv_after.tv_sec - tv_before.tv_sec) * 1000000;
	if (tv_after.tv_usec >= tv_before.tv_usec)
		time += tv_after.tv_usec - tv_before.tv_usec;
	else
		time += (tv_after.tv_usec + 1000000) - tv_before.tv_usec - 1000000;

	tp = ((float)count * bs) / time;
	printf("total time = %f, throughput = %.2f MBps\n", (float)time/1000000, tp);

	if (fd > 0)
		close(fd);
		
	return 0;
}
