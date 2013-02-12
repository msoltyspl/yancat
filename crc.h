/*
 * Copyright 2012 Michal Soltys <soltys@ziu.info>
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
#ifndef __crc_h__
#define __crc_h__

#include <stdint.h>
#include "common.h"

#define CRCBITS 32
#define CRC_REFIN 0
#define CRC_REFOUT 0

#if CRCBITS <= 32
# define CRCINT uint32_t
#elif CRCBITS <= 64
# define CRCINT uint64_t
#else
/* # define CRCINT unsigned __int128 */
# error "> 64 bit ints not supported ..."
#endif

extern CRCINT ctab[256];

void crc_init(void);
CRCINT crc_beg(void);
CRCINT crc_cksum(CRCINT, uint64_t);
CRCINT crc_end(CRCINT);
CRCINT crc_str(const char * restrict);

static inline CRCINT
crc_calc(CRCINT c, const uint8_t * restrict d, size_t cnt)
{
#if CRC_REFIN == 1
	while likely(cnt--)
		c = (c >> 8) ^ ctab[(c ^ *d++) & 0xFF];
#else
	while likely(cnt--)
		c = (c << 8) ^ ctab[(c >> (CRCBITS - 8) ^ *d++) & 0xFF];
#endif
	return c;
}

#endif
