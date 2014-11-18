/*
 * Copyright 2012+ Michal Soltys <soltys@ziu.info>
 *
 * This file is part of Yancat.
 *
 * Yancat is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * Yancat is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Yancat. If not, see <http://www.gnu.org/licenses/>.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include "crc.h"

#define CRCMASK (((((CRCINT)1<<(CRCBITS-1))-1)<<1)|1)

#define CRC_XORIN  0
#define CRC_XOROUT (~((CRCINT)0))

CRCINT ctab[256];
/* CRC-32 std */
static const CRCINT poly = 0x04C11DB7;
#if 0
/* CRC-40/GSM */
static const CRCINT poly = 0x0004820009;
/* CRC-64/XZ */
static const CRCINT poly = 0x42F0E1EBA9EA3693;
/* CRC-82/DARC */
static const CRCINT poly = 0x0308C0111011401440411;
#endif

#if (CRC_REFIN == 1) || (CRC_REFIN ^ CRC_REFOUT == 1)
static CRCINT reflect(CRCINT dat, int siz)
{
	int i;
	CRCINT val = 0;
	for(i = 0; i < siz; i++) {
		val = (val << 1) | (dat & 1);
		dat >>= 1;
	}
	return val;
}
#endif

void crc_init(void)
{
	int j;
	CRCINT i, c, tmp;

	for(i = 0; i < 256; i++) {
#if CRC_REFIN == 1
		c = reflect(i, 8) << (CRCBITS - 8);
#else
		c = i << (CRCBITS - 8);
#endif
		for(j = 0; j < 8; j++) {
			tmp = c >> (CRCBITS - 1) & 1;
			c <<= 1;
			if(tmp)
				c = c ^ poly;
		}
#if CRC_REFIN == 1
		c = reflect(c, CRCBITS);
#endif
		ctab[i] = c;
	}
}

CRCINT crc_cksum(CRCINT c, uint64_t b)
{
	size_t cnt = 0;
	uint8_t s[8];

	while (b) {
		s[cnt++] = b & 0xFF;
		b >>= 8;
	}
	c = crc_calc(c, s, cnt);
	return c;
}

CRCINT crc_beg(void)
{
	return CRC_XORIN;
}

CRCINT crc_end(CRCINT c)
{
#if CRC_REFIN ^ CRC_REFOUT == 1
	c = reflect(c, CRCBITS);
#endif
	return (c ^ CRC_XOROUT) & CRCMASK;
}

CRCINT crc_str(const char * restrict ptr)
{
	size_t len = strlen(ptr);
	return crc_end(crc_cksum(crc_calc(crc_beg(), (const uint8_t *restrict)ptr, len), len));
}


#if 0
int main (void)
{
	CRCINT c;
	const char check[]="123456789";
	crc_init();
	c = crc_beg();
	c = crc_calc(c, (uint8_t *)check, 9);
	c = crc_cksum(c, 9);
	c = crc_end(c);

	printf("%X\n", c);

	return 0;
}
#endif
