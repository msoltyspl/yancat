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
		/* hav = (~emp + 1) & mask; equivalent of hav > buf->rsp */
		if likely(emp < buf->rsp_inv)
			return 0;
		DEB2("read resumed: fill  %zu\n", (buf->got - buf->did) & buf->mask);
		buf->rstall = 0;
		/* min. free space is guaranteed after options' parsing */
		return buf->rblk;
	}
	if likely(emp > buf->rblk)
		return buf->rblk;
	if likely(emp > buf->rmin)
		return emp;
	if unlikely(buf->rsp) {
		/* overrun, back off */
		buf->rstall = 1;
		DEB2("read stalled: fill    %zu\n", (buf->got - buf->did) & buf->mask);
	}
	return 0;
}

size_t buf_can_w(struct buf_s * restrict buf)
{
	size_t hav;

	hav = (buf->got - buf->did) & buf->mask;

	if unlikely(buf->wstall) {
		if likely(hav < buf->wsp)
			return 0;
		DEB2("write resumed: fill %zu\n", hav);
		buf->wstall = 0;
		/* min. free space is guaranteed after options' parsing */
		return buf->wblk;
	}
	if likely(hav >= buf->wblk)
		return buf->wblk;
	if likely(hav >= buf->wmin)
		return hav;
	if unlikely(buf->wsp) {
		/* underrun, back off */
		buf->wstall = 1;
		DEB2("write stalled: fill   %zu\n", hav);
	}
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
	size_t csiz, page = get_page(), blk;

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

	// TODO sharp should be enough ... verify this
	blk = Y_MAX(rblk, wblk);
	if (bsiz <= 2*blk) {
		fputs("Buffer size must be greater than 2*max(rblk, wblk),\n"
		      "  to avoid corner cases.\n", stderr);
		goto out;
	}

	csiz = Y_ALIGN(rblk, page) + Y_ALIGN(wblk, page);
	if (shmw_ctor(&buf->scr, "/yancat-bounce-area", &csiz, 0, 0, 0) < 0) {
		fputs("buf: failed to allocate shared memory bounce area\n", stderr);
		goto out;
	}
	buf->rchunk = shmw_ptr(&buf->scr);
	buf->wchunk = buf->rchunk + Y_ALIGN(rblk, page);

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

	/* we can't assume we're at the edge, as read may return less (even if buf_fetch_r() checked for it) */
	siz1 = Y_MIN(buf->size - buf->got, chunk);
	siz2 = chunk - siz1;
	memcpy(buf->ptr + buf->got, buf->rchunk, siz1);
	if likely(siz2)
		memcpy(buf->ptr, buf->rchunk + siz1, siz2);

	if unlikely(buf->dorcrc)
		buf->rcrc = crc_calc(buf->rcrc, buf->rchunk, chunk);
}

/* fetch can be forced, we can't assume that it's at the edge */
uint8_t *ibuf_fetch_wbounce(struct buf_s *restrict buf, size_t chunk)
{
	size_t siz1, siz2;

	siz1 = Y_MIN(buf->size - buf->did, chunk);
	siz2 = chunk - siz1;
	memcpy(buf->wchunk, buf->ptr + buf->did, siz1);
	if likely(siz2)
		memcpy(buf->wchunk + siz1, buf->ptr, siz2);

	buf->fastw = 0;
	return buf->wchunk;
}

void buf_setlinew(struct buf_s *buf)
{
	buf->flags |= M_LINEW;
	buf->wmin = 1;
	buf->wsp = 0;
#if DEBUG == 2
	if (buf->wstall)
		DEB2("write forcibly (epilogue) resumed: fill %zu\n", (buf->got - buf->did) & buf->mask);
#endif
	buf->wstall = 0;
}

int buf_setextra(struct buf_s *buf, int rline, int wline, int rcrc, int wcrc, double rs, double ws)
{
	size_t rsp, wsp;
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
	rsp = (size_t)(0.5 + rs*(double)buf->size);
	if (rsp) {
		/*
		 * write will stop at nodata when wblk-1 or less data is ready;
		 * we must restart reads then; rsp as below does that at
		 * wblk+1;
		 * at the same time we need at least rblk+1 (+1 due to
		 * ambiguity) space to read
		 * basic condition: hav(dec) <= rsp to resume reads
		 */
		if (rsp <= buf->wblk || rsp >= buf->size - buf->rblk) {
			fputs("Read resume point is too small or leaves not enough space.\n", stderr);
			return -1;
		}
		buf->rsp_inv = buf->size - rsp;
		buf->rsp = rsp;
	}
	wsp = (size_t)(0.5 + ws*(double)buf->size);
	if (wsp) {
		/*
		 * read will stop at nospace when rblk-1 or less space is
		 * available; as buffer will never be fully filled (due to hav
		 * == got ambiguity) nospace will happen at rblk;
		 * we must restarts writes then; wsp as below does that at
		 * rblk+1;
		 * at the same time we need at least wblk data to write (wblk+1
		 * below)
		 * basic condition: hav(inc) >= wsp to resume writes
		 */
		if (wsp >= buf->size - buf->rblk || wsp <= buf->wblk) {
			fputs("Write resume point is too big or leaves.\n", stderr);
			return -1;
		}
		buf->wsp = wsp;
		buf->wstall = 1;
		DEB2("write forcibly (pre-run) stalled: fill 0\n");
	}
	return 0;
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
		"  re. unstall @: %zd\n"
		"  wr. unstall @: %zd\n"
		"  shared:    %s\n"
		"  wrapped:   %s\n"
		"  huge page: %s\n",
		buf->size,
		buf->rblk, buf->flags & M_LINER ? " (byte/line mode)" : "",
		buf->wblk, buf->flags & M_LINEW ? " (byte/line mode)" : "",
		buf->rsp ? (ssize_t)buf->rsp : -1,
		buf->wsp ? (ssize_t)buf->wsp : -1,
		buf->flags & M_SHM ? "yes" : "no",
		buf->flags & M_CIR ? "yes" : "no",
		buf->flags & M_HUGE ? "yes" : "no"
	);
}
