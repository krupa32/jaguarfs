#ifndef JAGUAR_H
#define JAGUAR_H

#define JAGUAR_BLOCK_SIZE		4096
#define JAGUAR_INODE_SIZE		128
#define JAGUAR_INODE_NUM_BLOCK_ENTRIES	15

#define BYTES_TO_BLOCK(b)		((b)/JAGUAR_BLOCK_SIZE)
#define JAGUAR_NUM_INODES_PER_BLOCK	(JAGUAR_BLOCK_SIZE / JAGUAR_INODE_SIZE)

#define INUM_TO_BLOCK(ino)		((ino) / JAGUAR_NUM_INODES_PER_BLOCK)
#define INUM_TO_OFFSET(ino)		(((ino) % JAGUAR_NUM_INODES_PER_BLOCK) * JAGUAR_INODE_SIZE)

#define INODE_TYPE_FILE			1
#define INODE_TYPE_DIR			2

/*
 * On-disk data structures.
 */

struct jaguar_super_block_on_disk
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
};

struct jaguar_inode_on_disk
{
	int size;
	int type;
	unsigned int blocks[JAGUAR_INODE_NUM_BLOCK_ENTRIES];
	char rsvd[60];
};

struct jaguar_dentry_on_disk
{
	unsigned int inum;
	char name[28];
};

/*
 * In-memory data structures
 */
struct jaguar_super_block
{
	struct jaguar_super_block_on_disk disk_copy;
};

struct jaguar_inode
{
	struct jaguar_inode_on_disk disk_copy;
};


/*
 * Super block APIs
 */
int jaguar_sb_read(struct super_block *sb); 

/*
 * Inode APIs
 */
struct inode * jaguar_iget(struct super_block *sb, int inum);
int jaguar_inode_read(struct inode *i); 

/*
 * Utility APIs
 */
int jaguar_find_first_zero_bit(void *bmap, int size);
void jaguar_set_bit(void *bmap, int pos);
void jaguar_clear_bit(void *bmap, int pos);

#endif // JAGUAR_H
