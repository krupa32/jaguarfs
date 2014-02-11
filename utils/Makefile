
all: mkfs.jaguar jagadm jcat jls

jls: jls.c
	gcc jls.c -o jls

jcat: jcat.c
	gcc jcat.c -o jcat

jagadm: jagadm.c
	gcc jagadm.c -o jagadm

mkfs.jaguar: mkfs.c
	gcc mkfs.c -o mkfs.jaguar

clean:
	rm -f mkfs.jaguar jagadm jcat jls
