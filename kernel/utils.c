#include <linux/fs.h>
#include "jaguar.h"
#include "debug.h"

int jaguar_find_first_zero_bit(void *buf, int size)
{
	int ret, i = 0;
	unsigned char mask, *bmap = (unsigned char *)buf;

	/* find 1st byte with a 0 bit */
	while (i < size && *(bmap + i) == 0xFF)
		i++;

	if (i == size)
		return -1;

	ret = i * 8;
	mask = 0x80;
	while ( *(bmap + i) & mask) {
		mask = mask >> 1;
		ret++;
	}

	DBG("jaguar_find_first_zero_bit: returning %d\n", ret);
	return ret;
}

void jaguar_set_bit(void *buf, int pos)
{
	int byte, bit;
	unsigned char mask, *bmap = (unsigned char *)buf;

	byte = pos / 8;
	bit = pos % 8;
	mask = 0x80;
	while (bit--)
		mask = mask >> 1;

	DBG("jaguar_set_bit: pos=%d, byte=%d, mask=0x%x\n", pos, byte, mask);
	bmap[byte] |= mask;
}

void jaguar_clear_bit(void *buf, int pos)
{
	int byte, bit;
	unsigned char mask, *bmap = (unsigned char *)buf;

	byte = pos / 8;
	bit = pos % 8;
	mask = 0x80;
	while (bit--)
		mask = mask >> 1;

	DBG("jaguar_clear_bit: pos=%d, byte=%d, mask=0x%x\n", pos, byte, mask);
	bmap[byte] &= ~mask;
}
