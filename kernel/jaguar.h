#ifndef JAGUAR_H
#define JAGUAR_H

#include <linux/ioctl.h>

#define JAGUAR_MAGIC			0x4a41 // JA
#define JAGUAR_BLOCK_SIZE		4096
#define JAGUAR_INODE_SIZE		128
#define JAGUAR_INODE_NUM_BLOCK_ENTRIES	15
#define JAGUAR_FILENAME_MAX		60

#define BYTES_TO_BLOCK(b)		((b)/JAGUAR_BLOCK_SIZE)
#define JAGUAR_NUM_INODES_PER_BLOCK	(JAGUAR_BLOCK_SIZE / JAGUAR_INODE_SIZE)
#define NUM_BITS_PER_BLOCK		(JAGUAR_BLOCK_SIZE * 8)

#define INUM_TO_BLOCK(ino)		((ino) / JAGUAR_NUM_INODES_PER_BLOCK)
#define INUM_TO_OFFSET(ino)		(((ino) % JAGUAR_NUM_INODES_PER_BLOCK) * JAGUAR_INODE_SIZE)

#define INODE_TYPE_FILE			1
#define INODE_TYPE_DIR			2

#define INODE_FLG_VERSIONED		0x1

/* 
 * ioctls
 */
#define JAGUAR_IOC_VERSION		_IOW('f', 100, int)
#define JAGUAR_IOC_UNVERSION		_IO('f', 101)
#define JAGUAR_IOC_RETRIEVE		_IOWR('f', 102, int)
#define JAGUAR_IOC_PRUNE		_IO('f', 103)

/*
 * version flags
 */
#define JAGUAR_KEEP_ALL			1
#define JAGUAR_KEEP_SAFE_VERSIONS	2
#define JAGUAR_KEEP_SAFE_TIME		3

//#define VERSION_METADATA_MAX_ENTRIES	255
#define VERSION_METADATA_MAX_ENTRIES	3

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

	int n_blocks;
	int n_blocks_free;
	int n_inodes;
	int n_inodes_free;

	int next_free_block;
	int next_free_inode;

};

struct jaguar_inode_on_disk
{
	int size;
	int type;
	int nlink;
	unsigned int blocks[JAGUAR_INODE_NUM_BLOCK_ENTRIES];
	int ver_meta_block;
	int version_type;	/* one of JAGUAR_KEEP_xxx */
	int version_param;	/* depends on version type */
	char rsvd[44];
};

struct jaguar_dentry_on_disk
{
	unsigned int inum;
	char name[JAGUAR_FILENAME_MAX];
};

struct jaguar_version_metadata
{
	int num_entries;
	int start_entry;
	int next_block;

	struct jaguar_version_metadata_entry
	{
		int logical_block;
		int version_block;
		int timestamp;
		int bytes_valid;
	} entry[VERSION_METADATA_MAX_ENTRIES];

	char rsvd[4];
};

/*
 * In-memory data structures
 */
struct jaguar_super_block
{
	struct buffer_head *bh;
	struct jaguar_super_block_on_disk *disk_copy;
};

struct jaguar_inode
{
	struct jaguar_inode_on_disk disk_copy;
	struct buffer_head *ver_meta_bh;
	char *ver_data_buf;
};

struct version_buffer 
{
	int offset;
	int at;
	char data[JAGUAR_BLOCK_SIZE];
};

struct version_info
{
	int type;
	int param;
};



/*
 * Super block APIs
 */
int jaguar_fill_super(struct super_block *sb, void *data, int silent);


/*
 * Inode APIs
 */
struct inode * jaguar_iget(struct super_block *sb, int inum);
int write_inode_to_disk(struct inode *i);

/*
 * Data block APIs
 */
int alloc_data_block(struct super_block *sb);
int free_data_block(struct super_block *sb, int block);

/*
 * Version metadata block APIs
 */
int alloc_version_meta_block(struct inode *i);

/*
 * Utility APIs
 */
int jaguar_find_first_zero_bit(void *bmap, int size);
void jaguar_set_bit(void *bmap, int pos);
void jaguar_clear_bit(void *bmap, int pos);
int jaguar_bmap_alloc_bit(struct block_device *bdev,
	int bmap_start, int bmap_size, int start);
int jaguar_bmap_free_bit(struct block_device *bdev, int bmap_start, int bit);

/*
 * Versioning APIs
 */
int set_version(struct inode *i, struct version_info *info);
int reset_version(struct inode *i);
int retrieve(struct file *filp, int logical_block, int at, char __user *data);
int prune(struct file *filp);


#endif // JAGUAR_H
