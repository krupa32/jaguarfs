
all: mkfs.jaguar jagadm

jagadm: jagadm.c
	gcc jagadm.c -o jagadm

mkfs.jaguar: mkfs.c
	gcc mkfs.c -o mkfs.jaguar

clean:
	rm -f mkfs.jaguar jagadm
