
all: mkfs.jaguar jagadm jcat

jcat: jcat.c
	gcc jcat.c -o jcat

jagadm: jagadm.c
	gcc jagadm.c -o jagadm

mkfs.jaguar: mkfs.c
	gcc mkfs.c -o mkfs.jaguar

clean:
	rm -f mkfs.jaguar jagadm jcat
