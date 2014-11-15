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
#include "config.h"
#ifndef __buffer_h__
#define __buffer_h__

#include <stdint.h>
#include "common.h"
#include "crc.h"
#include "shmw.h"

#define M_LINER 0x01
#define M_LINEW 0x02
#define M_HUGE  0x04
#define M_SHM   0x08
#define M_CIR   0x10

struct buf_s {
	struct shm_s buf, scr; /* buf and bounce areas */
	uint8_t *ptr, *rchunk, *wchunk;
	size_t size, mask;
	size_t got, did;
	size_t rblk, wblk, rmin, wmin, rsp, wsp;
	int fastr, fastw, flags, dorcrc, dowcrc, iscir, wstall, rstall;
	unsigned long long int allin, allout;
	CRCINT rcrc, wcrc;
};

void ibuf_commit_rbounce(struct buf_s *restrict buf, size_t chunk);
uint8_t * ibuf_fetch_wbounce(struct buf_s * restrict buf, size_t chunk);

/*
 * on mingw we support only subset of full functionality - among those there's
 * no possibility for circular buffer; OTOH on any unix it's almost guaranteed
 */
static inline uint8_t *
buf_fetch_r(struct buf_s *restrict buf, size_t chunk)
{
#ifdef h_mingw
	if (likely(buf->got + chunk <= buf->size) || buf->iscir) {
#else
	if (likely(buf->iscir) || buf->got + chunk <= buf->size) {
#endif
		buf->fastr = 1;
		return buf->ptr + buf->got;
	} else {
		buf->fastr = 0;
		return buf->rchunk;
	}
}

/* see comment above */
static inline uint8_t *
buf_fetch_w(struct buf_s * restrict buf, size_t chunk)
{
#ifdef h_mingw
	if (likely(buf->did + chunk <= buf->size) || buf->iscir) {
#else
	if (likely(buf->iscir) || buf->did + chunk <= buf->size) {
#endif
		buf->fastw = 1;
		return buf->ptr + buf->did;
	} else
		return ibuf_fetch_wbounce(buf, chunk);
}

static inline void
buf_commit_r(struct buf_s *restrict buf, size_t chunk)
{
	buf->allin += chunk;
	if likely(buf->fastr) {
		if unlikely(buf->dorcrc)
			buf->rcrc = crc_calc(buf->rcrc, buf->ptr + buf->got, chunk);
	} else
		ibuf_commit_rbounce(buf, chunk);
}

static inline void
buf_commit_w(struct buf_s * restrict buf, size_t chunk)
{
	buf->allout += chunk;
	if unlikely(buf->dowcrc) {
		if likely(buf->fastw) {
			buf->wcrc = crc_calc(buf->wcrc, buf->ptr + buf->did, chunk);
		} else {
			buf->wcrc = crc_calc(buf->wcrc, buf->wchunk, chunk);
		}
	}
}
/*
 * commit functions are split into r/rf and w/wf, because .f versions
 * modify values and we have no guarantees about atomicity - at the same time
 * funcions making decisions (buf_can_r/w) would be reading those values
 */
static inline void
buf_commit_rf(struct buf_s *restrict buf, size_t chunk)
{
	buf->got = (buf->got + chunk) & buf->mask;
}

static inline void
buf_commit_wf(struct buf_s *restrict buf, size_t chunk)
{
	buf->did = (buf->did + chunk) & buf->mask;
}

/* common interface follows */

int  buf_ctor(struct buf_s *buf, size_t bsiz, size_t rblk, size_t wblk, size_t hpage);
void buf_dtor(struct buf_s *buf);
void buf_dt(struct buf_s *buf);

void buf_setextra(struct buf_s *buf, int rline, int wline, int rcrc, int wcrc, size_t rsp, size_t wsp);
void buf_report_init(struct buf_s *buf);
void buf_report_stats(struct buf_s *buf);
void buf_setlinew(struct buf_s *buf);

size_t buf_can_r(struct buf_s *restrict buf);
uint8_t *buf_fetch_r(struct buf_s *restrict buf, size_t chunk);
void buf_commit_r(struct buf_s *restrict buf, size_t chunk);
void buf_commit_rf(struct buf_s *restrict buf, size_t chunk);

size_t buf_can_w(struct buf_s *restrict buf);
uint8_t *buf_fetch_w(struct buf_s *restrict buf, size_t chunk);
void buf_commit_w(struct buf_s *restrict buf, size_t chunk);
void buf_commit_wf(struct buf_s *restrict buf, size_t chunk);

#endif
