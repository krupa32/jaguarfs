all: overwrite create

overwrite: overwrite.c
	gcc $^ -o $@

create: create.c
	gcc $^ -o $@

clean:
	rm overwrite create
