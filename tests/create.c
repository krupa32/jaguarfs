#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_NUM_FILES	4096
#define NUM_ITERATIONS		1

int main(int argc, char **argv)
{
	int fd, num_files = DEFAULT_NUM_FILES, count, i, j, time;
	struct timeval tv_before, tv_after;
	float tp;
	char filename[32];

	if (argc > 1)
		num_files = strtol(argv[1], NULL, 10);

	count = NUM_ITERATIONS;

	printf("starting create test with num files %d, count %d\n", num_files, count);

	gettimeofday(&tv_before, NULL);

	for (i = 0; i < count; i++) {
		
		for (j = 0; j < num_files; j++) {
			sprintf(filename, "test%d.out", j);
			if ((fd = creat(filename, S_IRUSR|S_IWUSR)) < 0) {
				perror(NULL);
				return -1;
			}

			close(fd);
		}

		for (j = 0; j < num_files; j++) {
			sprintf(filename, "test%d.out", j);
			unlink(filename);
		}
	}

	gettimeofday(&tv_after, NULL);

	time = (tv_after.tv_sec - tv_before.tv_sec) * 1000000;
	if (tv_after.tv_usec >= tv_before.tv_usec)
		time += tv_after.tv_usec - tv_before.tv_usec;
	else
		time += (tv_after.tv_usec + 1000000) - tv_before.tv_usec - 1000000;

	tp = ((float)count * num_files) / time * 1000000;
	printf("total time = %f, rate = %.2f files/sec\n", (float)time/1000000, tp);

		
	return 0;
}
