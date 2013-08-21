#ifndef JAGUAR_H
#define JAGUAR_H

#define JAGUAR_BLOCK_SIZE		4096
#define JAGUAR_INODE_NUM_BLOCK_ENTRIES	15
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
	unsigned int blocks[JAGUAR_INODE_NUM_BLOCK_ENTRIES];
	char rsvd[64];
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
int jaguar_inode_read(struct inode *i); 

#endif // JAGUAR_H
