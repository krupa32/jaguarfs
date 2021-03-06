/*
 * mkfs.c
 * Krupa Sivakumaran, Aug 14 2013
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define BLK_SIZE		4096
#define INODE_SIZE		128
#define INODE_NUM_BLOCK_ENTRIES	15

#define INODE_TYPE_FILE		1
#define INODE_TYPE_DIR		2

#define ROUND_TO_BLK_SIZE(s)		(((s) + BLK_SIZE - 1) / BLK_SIZE * BLK_SIZE)

#define DENTRY_TYPE_DIR		2

struct super_block
{
	char name[16];
	int sb_start;
	int sb_size;
	int data_bmap_start;
	int data_bmap_size;
	int inode_bmap_start;
	int inode_bmap_size;
	int inode_tbl_start;
	int inode_tbl_size;
	int data_start;

	int n_blocks;
	int n_blocks_free;
	int n_inodes;
	int n_inodes_free;

	int next_free_block;
	int next_free_inode;
};

struct disk_inode
{
	int size;
	int type;
	int nlink;
	unsigned int blocks[INODE_NUM_BLOCK_ENTRIES];
	int ver_meta_block;
	int version_type;
	int version_param;
	char rsvd[44];
};

struct dentry
{
	unsigned int inode;
	char name[60];
};

int fill_super_block(struct super_block *sb, int disk_size)
{
	int max_inodes, max_blks, metadata_size;
	int data_bmap_size, inode_bmap_size, inode_tbl_size;

	max_inodes = disk_size / 8192;
	max_blks = disk_size / BLK_SIZE;
	data_bmap_size = ROUND_TO_BLK_SIZE(max_blks / 8);
	inode_bmap_size = ROUND_TO_BLK_SIZE(max_inodes / 8);
	inode_tbl_size = ROUND_TO_BLK_SIZE(max_inodes * INODE_SIZE);

	printf("disk size = %d\n", disk_size);
	printf("max_blocks = %d, data_bmap_size = %d\n",
		max_blks, data_bmap_size);
	printf("max_inodes = %d, inode_bmap_size = %d, inode_tbl_size = %d\n",
		max_inodes, inode_bmap_size, inode_tbl_size);


	strcpy(sb->name, "jaguarfs");

	sb->sb_start = 0;
	sb->sb_size = BLK_SIZE;
	printf("super block: start = %d, size = %d\n", sb->sb_start, sb->sb_size);

	sb->data_bmap_start = sb->sb_start + sb->sb_size;
	sb->data_bmap_size = data_bmap_size;
	printf("data bitmap: start = %d, size = %d\n", sb->data_bmap_start, sb->data_bmap_size);

	sb->inode_bmap_start = sb->data_bmap_start + sb->data_bmap_size;
	sb->inode_bmap_size = inode_bmap_size;
	printf("inode bitmap: start = %d, size = %d\n", sb->inode_bmap_start, sb->inode_bmap_size);

	sb->inode_tbl_start = sb->inode_bmap_start + sb->inode_bmap_size;
	sb->inode_tbl_size = inode_tbl_size;
	printf("inode table: start = %d, size = %d\n", sb->inode_tbl_start, sb->inode_tbl_size);

	sb->data_start = sb->inode_tbl_start + sb->inode_tbl_size;
	printf("data: start = %d\n", sb->data_start);

	metadata_size = BLK_SIZE + data_bmap_size + inode_bmap_size + inode_tbl_size;
	sb->n_blocks = max_blks;
	sb->n_blocks_free = max_blks - (metadata_size / BLK_SIZE);
	sb->next_free_block = metadata_size / BLK_SIZE;
	printf("blocks: total = %d, free = %d, next = %d\n", sb->n_blocks, sb->n_blocks_free, sb->next_free_block);

	sb->n_inodes = max_inodes;
	sb->n_inodes_free = max_inodes - 2;
	sb->next_free_inode = 2;
	printf("inodes: total = %d, free = %d, next = %d\n", sb->n_inodes, sb->n_inodes_free, sb->next_free_inode);

	return 0;
}

int write_super_block(FILE *fp, struct super_block *sb)
{
	fseek(fp, sb->sb_start, SEEK_SET);

	if (fwrite(sb, sizeof(*sb), 1, fp) != 1)
		return -1;

	return 0;
}

void set_bit(unsigned char *buf, int pos)
{
	int index, offset;
	unsigned char mask;

	index = pos / 8;
	offset = pos % 8;
	mask = 0x80 >> offset;

	buf[index] |= mask;
}

int write_data_bmap(FILE *fp, struct super_block *sb)
{
	unsigned char *data_bmap;
	int i, n_fs_blks;

	if ((data_bmap = malloc(sb->data_bmap_size)) == NULL) {
		errno = -ENOMEM;
		return -1;
	}
	memset(data_bmap, 0, sb->data_bmap_size);

	n_fs_blks = sb->data_start / BLK_SIZE;
	printf("num filesystem metadata blocks = %d\n", n_fs_blks);

	for (i = 0; i < n_fs_blks; i++)
		set_bit(data_bmap, i);

	/* the first data block is also reserved.
	 * it holds the dentry for root dir.
	 */
	set_bit(data_bmap, n_fs_blks);

	fseek(fp, sb->data_bmap_start, SEEK_SET);
	if (fwrite(data_bmap, sb->data_bmap_size, 1, fp) != 1) {
		free(data_bmap);
		return -1;
	}

	free(data_bmap);

	return 0;
}

int write_inode_bmap(FILE *fp, struct super_block *sb)
{
	unsigned char *inode_bmap;

	if ((inode_bmap = malloc(sb->inode_bmap_size)) == NULL) {
		errno = -ENOMEM;
		return -1;
	}
	memset(inode_bmap, 0, sb->inode_bmap_size);

	*inode_bmap |= 0xC0; // 0th inode is dummy, 1st inode is root inode

	fseek(fp, sb->inode_bmap_start, SEEK_SET);
	if (fwrite(inode_bmap, sb->inode_bmap_size, 1, fp) != 1) {
		free(inode_bmap);
		return -1;
	}

	free(inode_bmap);

	return 0;
}

int write_inode_table(FILE *fp, struct super_block *sb)
{
	struct disk_inode root_inode;

	memset(&root_inode, 0, sizeof(root_inode));
	root_inode.size = sizeof(struct dentry) * 2; /* for . and .. */
	root_inode.type = INODE_TYPE_DIR;
	root_inode.nlink = 1;
	root_inode.blocks[0] = sb->data_start / BLK_SIZE;

	/* skip one disk inode, as there is no inode 0.
	 * inum's only start with 1.
	 */
	fseek(fp, sb->inode_tbl_start + sizeof(root_inode), SEEK_SET);
	if (fwrite(&root_inode, sizeof(root_inode), 1, fp) != 1) {
		return -1;
	}

	return 0;

}

int write_data(FILE *fp, struct super_block *sb)
{
	struct dentry *root_dentry;
	char blkbuf[BLK_SIZE];

	memset(blkbuf, 0, BLK_SIZE);

	root_dentry = (struct dentry *)blkbuf;

	/* write out a dentry for . and .. */
	root_dentry->inode = 1; /* inum MUST start from 1 */
	strcpy(root_dentry->name, ".");
	root_dentry++;
	root_dentry->inode = 1;
	strcpy(root_dentry->name, "..");

	fseek(fp, sb->data_start, SEEK_SET);
	if (fwrite(blkbuf, BLK_SIZE, 1, fp) != 1) {
		return -1;
	}

	return 0;

}

int main(int argc, char **argv)
{
	FILE *fp;
	int ret = 0, disk_size;
	struct super_block sb;

	if (argc != 2) {
		printf("Usage: mkfs.jaguar FILE\n");
		ret = -EINVAL;
		goto err;
	}

	if ((fp = fopen(argv[1], "wb")) == NULL) {
		perror(NULL);
		ret = errno;
		goto err;
	}

	fseek(fp, 0, SEEK_END);
	disk_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	fill_super_block(&sb, disk_size);

	printf("writing super block...");
	if (write_super_block(fp, &sb) < 0) {	
		perror(NULL);
		ret = errno;
		goto fail;
	}
	printf("[ok]\n");

	printf("writing data bitmap...");
	if (write_data_bmap(fp, &sb) < 0) {
		perror(NULL);
		ret = errno;
		goto fail;
	}
	printf("[ok]\n");

	printf("writing inode bitmap...");
	if (write_inode_bmap(fp, &sb) < 0) {
		perror(NULL);
		ret = errno;
		goto fail;
	}
	printf("[ok]\n");

	printf("writing inode table...");
	if (write_inode_table(fp, &sb) < 0) {
		perror(NULL);
		ret = errno;
		goto fail;
	}
	printf("[ok]\n");

	printf("writing data (root dir entry)...");
	if (write_data(fp, &sb) < 0) {
		perror(NULL);
		ret = errno;
		goto fail;
	}
	printf("[ok]\n");


	fclose(fp);

	return 0;

fail:
	printf("[failed]\n");
	fclose(fp);
err:
	return ret;
}
