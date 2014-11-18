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
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "buffer.h"
#include "crc.h"

/*
 * we require sharp inequalities here, as otherwise we could end (after
 * reading) with got == did, and that implies empty buffer
 */
size_t buf_can_r(struct buf_s *restrict buf)
{
	size_t emp;

	emp = (buf->did - buf->got) & buf->mask;
	if unlikely(!emp)
		emp = buf->size;

	if unlikely(buf->rstall) {
		if likely(emp < buf->rsp)
			return 0;
		buf->rstall = 0;
		/* min. free space is guaranteed after options' parsing */
		return buf->rblk;
	}
	if likely(emp > buf->rblk)
		return buf->rblk;
	if likely(emp > buf->rmin)
		return emp;
	if unlikely(buf->rsp)
		/* overrun, back off */
		buf->rstall = 1;
	return 0;
}

size_t buf_can_w(struct buf_s * restrict buf)
{
	size_t hav;

	hav = (buf->got - buf->did) & buf->mask;

	if unlikely(buf->wstall) {
		if likely(hav < buf->wsp)
			return 0;
		buf->wstall = 0;
		/* min. free space is guaranteed after options' parsing */
		return buf->wblk;
	}
	if likely(hav >= buf->wblk)
		return buf->wblk;
	if likely(hav >= buf->wmin)
		return hav;
	if unlikely(buf->wsp)
		/* underrun, back off */
		buf->wstall = 1;
	return 0;
}

void buf_dt(struct buf_s *buf)
{
	if (!buf)
		return;
	shmw_dt(&buf->buf);
	if (!buf->iscir)
		shmw_dt(&buf->scr);
}

void buf_dtor(struct buf_s *buf)
{
	if (!buf)
		return;
	shmw_dtor(&buf->buf);
	if (!buf->iscir)
		shmw_dtor(&buf->scr);
	memset(buf, 0, sizeof *buf);
}

int buf_ctor(struct buf_s *buf, size_t bsiz, size_t rblk, size_t wblk, size_t hpage)
{
	int ret, idx;
	size_t csiz, page = get_page();

	if (!buf) {
		fputs("buf: no buf ?\n", stderr);
		return -1;
	}

	memset(buf, 0, sizeof *buf);

	if ((ret = shmw_ctor(&buf->buf, "/yancat-buf-area", &bsiz, hpage, 1, 0)) < 0) {
		fputs("buf: failed to allocate [shared] memory buffer area\n", stderr);
		return -1;
	}
	if ((idx = is_pow2(bsiz)) < 0) {
		fprintf(stderr, "buf: aligned buffer size must be power of 2, but is: 0x%zX\n", bsiz);
		goto out;
	}

	buf->ptr = shmw_ptr(&buf->buf);
	buf->size = bsiz;
	buf->mask = (((size_t)1 << idx) - 1);
	if (ret != 1)
		buf->flags |= M_SHM;
	if (hpage > page)
		buf->flags |= M_HUGE;
	if (shmw_cir(&buf->buf) >= 0) {
		buf->flags |= M_CIR;
		buf->iscir = 1;
	}

	if (!buf->iscir) {
		csiz = Y_ALIGN(rblk, page) + Y_ALIGN(wblk, page);
		if (shmw_ctor(&buf->scr, "/yancat-bounce-area", &csiz, 0, 0, 0) < 0) {
			fputs("buf: failed to allocate shared memory bounce area\n", stderr);
			goto out;
		}
		buf->rchunk = shmw_ptr(&buf->scr);
		buf->wchunk = buf->rchunk + Y_ALIGN(rblk, page);
	}

	buf->rblk = rblk;
	buf->wblk = wblk;
	return ret;
out:
	shmw_dtor(&buf->buf);
	return -1;
}

void ibuf_commit_rbounce(struct buf_s *restrict buf, size_t chunk)
{
	size_t siz1, siz2;

	if unlikely(buf->got + chunk <= buf->size) {
		/*
		 * this is necessary, as the read may return less than what we
		 * fetched; in such case, 2nd memcpy below would be invalid
		 */
		memcpy(buf->ptr + buf->got, buf->rchunk, chunk);
	} else {
		siz1 = buf->size - buf->got;
		siz2 = chunk - siz1;
		memcpy(buf->ptr + buf->got, buf->rchunk, siz1);
		memcpy(buf->ptr, buf->rchunk + siz1, siz2);
	}

	if unlikely(buf->dorcrc)
		buf->rcrc = crc_calc(buf->rcrc, buf->rchunk, chunk);
}

uint8_t *ibuf_fetch_wbounce(struct buf_s *restrict buf, size_t chunk)
{
	size_t siz1, siz2;

	siz1 = buf->size - buf->did;
	siz2 = chunk - siz1;
	memcpy(buf->wchunk, buf->ptr + buf->did, siz1);
	memcpy(buf->wchunk + siz1, buf->ptr, siz2);

	buf->fastw = 0;
	return buf->wchunk;
}
void buf_setlinew(struct buf_s *buf)
{
	buf->flags |= M_LINEW;
	buf->wmin = 1;
	buf->wsp = 0;
	buf->wstall = 0;
}

void buf_setextra(struct buf_s *buf, int rline, int wline, int rcrc, int wcrc, size_t rsp, size_t wsp)
{
	if (rline) {
		buf->flags |= M_LINER;
		buf->rmin = 1;
	} else
		buf->rmin = buf->rblk;

	if (wline) {
		buf->flags |= M_LINEW;
		buf->wmin = 1;
	} else
		buf->wmin = buf->wblk;

	if (rcrc) {
		buf->rcrc = crc_beg();
		buf->dorcrc = 1;
	}
	if (wcrc) {
		buf->wcrc = crc_beg();
		buf->dowcrc = 1;
	}
	if (rsp) {
		buf->rsp = rsp;
	}
	if (wsp) {
		buf->wsp = wsp;
		buf->wstall = 1;
	}
}

static void rep_crc(int c, CRCINT _crc, unsigned long long cnt)
{
	unsigned long long int crc, cks;
	crc = crc_end(_crc);
	cks = crc_end(crc_cksum(_crc, cnt));
	fprintf (stderr,
		"  %ccrc:  0x%llx; %ccksum: %llu (0x%llx)\n",
		c, crc, c, cks, cks
	);
}

void buf_report_stats(struct buf_s * restrict buf)
{
	fprintf (stderr,
		"Buffer stats:\n"
		"  read:  %llu (%llu%c blocks)\n"
		"  wrote: %llu (%llu%c blocks)\n",
		buf->allin,  buf->allin / buf->rblk,  buf->allin  % buf->rblk ? '+':' ',
		buf->allout, buf->allout / buf->wblk, buf->allout % buf->wblk ? '+':' '
	);
	if (buf->dorcrc)
		rep_crc('r', buf->rcrc, buf->allin);
	if (buf->dowcrc)
		rep_crc('w', buf->wcrc, buf->allout);
}

void buf_report_init(struct buf_s * restrict buf)
{
	fprintf (stderr,
		"Buffer setup:\n"
		"  size:      %zu\n"
		"  re. block: %zu%s\n"
		"  wr. block: %zu%s\n"
		"  shared:    %s\n"
		"  wrapped:   %s\n"
		"  huge page: %s\n",
		buf->size,
		buf->rblk, buf->flags & M_LINER ? " (byte/line mode)" : "",
		buf->wblk, buf->flags & M_LINEW ? " (byte/line mode)" : "",
		buf->flags & M_SHM ? "yes" : "no",
		buf->flags & M_CIR ? "yes" : "no",
		buf->flags & M_HUGE ? "yes" : "no"
	);
}
