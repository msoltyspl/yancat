#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifndef h_mingw
//# include <sys/mman.h>
# include <sys/ipc.h>
# include <sys/shm.h>
#if 0
# ifndef MAP_ANONYMOUS
#  define MAP_ANONYMOUS MAP_ANON
# endif
#endif
#endif

#include "common.h"
#include "buffer.h"
#include "crc.h"

#if 0
#ifndef MAP_SEMAPHORE
# define MAP_SEMAPHORE 0
#endif
#endif

#if 0
#ifndef MAP_HUGETLB
# ifdef h_linux
#  define MAP_HUGETLB 0x40000
# else
#  define MAP_HUGETLB 0
# endif
#endif
#endif

#ifndef SHM_HUGETLB
# ifdef h_linux
#  define SHM_HUGETLB 04000
# else
#  define SHM_HUGETLB 0
# endif
#endif

#define ALIGN(x, a) (((uintptr_t)(x) + a - 1) & ~((uintptr_t)(a) - 1))
#define ISALI(x, a) (!((uintptr_t)(x) & ~((uintptr_t)(a) - 1)))

/* returns bit index, starting with 0 */
static int is_pow2(size_t x)
{
	size_t y = 1;
	int cnt = 0, pos = 0, is, posr = 0;

	while (y) {
		is = (x & y) > 0;
		if (is) {
			cnt++;
			posr = pos;
		}
		y <<= 1;
		pos++;
	}
	return cnt == 1 ? posr : posr | INT_MIN;
}

static void preset_mal(struct buf_s *buf)
{
	buf->idC = -1;
	buf->idB = -1;
	buf->mapB = NULL;
	buf->mapW = NULL;
	buf->mapC = NULL;
}

static void preset_shm(struct buf_s *buf)
{
	buf->idC = -1;
	buf->idB = -1;
	buf->mapB = (void *)-1;
	buf->mapW = (void *)-1;
	buf->mapC = (void *)-1;
}

static void detach_mal(struct buf_s *buf)
{
	if (buf->mapC)
		free(buf->mapC);
	if (buf->mapB)
		free(buf->mapB);
	buf->mapB = NULL;
	buf->mapC = NULL;
}

static void detach_shm(struct buf_s *buf)
{
#ifndef h_mingw
	if (buf->mapC != (void *)-1)
		shmdt(buf->mapC);
	if (buf->mapB != (void *)-1)
		shmdt(buf->mapB);
	if (buf->mapW != (void *)-1)
		shmdt(buf->mapW);
	/*
	 * we don't mark them as freed (-1), as many processes might want
	 * to detach the area; contrary to usual malloc, it's safe
	 */
#if 0
	buf->mapB = (void *)-1;
	buf->mapW = (void *)-1;
	buf->mapC = (void *)-1;
#endif
#else
	return;
#endif
}

static void rm_shm(struct buf_s *buf)
{
#ifndef h_mingw
	DEB("\nshm clean: C:%p, B:%p, B+S:%p, W:%p\n", buf->mapC, buf->mapB, buf->mapB + buf->size, buf->mapW);

	if (buf->idB != -1)
		shmctl(buf->idB, IPC_RMID, NULL);
	if (buf->idC != -1)
		shmctl(buf->idC, IPC_RMID, NULL);
#if 0
	buf->idC = -1;
	buf->idB = -1;
#endif
#else
	return;
#endif
}

void buf_dt(struct buf_s *buf)
{
	if (!buf)
		return;
	if (buf->mapW)
		detach_shm(buf);
	else
		detach_mal(buf);
}

void buf_dtor(struct buf_s *buf)
{
	if (!buf)
		return;
	if (buf->mapW) {
		detach_shm(buf);
		rm_shm(buf);
	} else
		detach_mal(buf);
}

static int setup_shm(struct buf_s *buf, size_t bsiza, size_t csiza, size_t hpage)
{
#ifndef h_mingw
	uint8_t *ptr;
	void *addr_a = NULL;
	int flags_g = IPC_CREAT | IPC_EXCL | SHM_R | SHM_W;
	int flags_a = SHM_RND;
	int ret = -1;

	if (hpage) {
		flags_g |= SHM_HUGETLB;
# if defined(__ia64__) && defined(h_linux)
		addr_a = (void *)(0x8000000000000000ULL);
# endif
	}

	buf->idC = shmget((key_t)crc_str("/yancat_chunk"), csiza, flags_g);
	if (buf->idC < 0) {
		perror("buf: shmget() chunka area");
		goto out;
	}
	buf->idB = shmget((key_t)crc_str("/yancat_buffer"), bsiza, flags_g);
	if (buf->idB < 0) {
		perror("buf: shmget() buffer");
		goto out;
	}

	buf->mapC = shmat(buf->idC, addr_a, flags_a);
	if (buf->mapC == (void *)-1) {
		perror("buf: shmat() chunk area");
		goto out;
	}
	buf->mapW = shmat(buf->idB, addr_a, flags_a);
	if (buf->mapW == (void *)-1) {
		perror("buf: shmat() buffer wrap-around");
		goto out;
	}
	/* try both above and below */
	buf->mapB = shmat(buf->idB, buf->mapW - bsiza, flags_a);
	if (buf->mapB == (void *)-1) {
		buf->mapB = shmat(buf->idB, buf->mapW + bsiza, flags_a);
		if (buf->mapB == (void *)-1) {
			perror("buf: shmat() buffer main");
			goto out;
		}
		ptr = buf->mapB;
		buf->mapB = buf->mapW;
		buf->mapW = ptr;
	}
	if (buf->mapB + bsiza != buf->mapW) {
		fputs("buf: cannot match wrap-around\n", stderr);
		goto out;
	}
	buf->buf  = buf->mapB;
	buf->chk  = buf->mapC;
	ret = 0;
out:
	return ret;
#else
	return -1;
#endif
}

static int setup_mal(struct buf_s *buf, size_t bsiza, size_t csiza, size_t adj)
{
	int ret = -1;

	csiza += adj;
	bsiza += adj;
	buf->mapB = (uint8_t *)malloc(bsiza);
	if (!buf->mapB) {
		fputs("buf: malloc()\n", stderr);
		goto out;
	}
	buf->buf = (uint8_t *)ALIGN(buf->mapB, adj);

	buf->mapC =  (uint8_t *)malloc(csiza);
	if (!buf->mapC) {
		fputs("buf: malloc()\n", stderr);
		goto out;
	}
	buf->chk = (uint8_t *)ALIGN(buf->mapC, adj);

	ret = 0;
out:
	return ret;
}

int buf_ctor(struct buf_s *buf, size_t bsiz, size_t rblk, size_t wblk, int rline, int wline, size_t hpage, size_t ioar, size_t ioaw)
{
	int ret = -1;
	size_t adj, page, idx, rblki, wblki, bsiza, csiza;

	if (!buf) {
		fputs("buf: no buf ?\n", stderr);
		return -1;
	}

	if (bsiz < 2*rblk || bsiz < 2*wblk) {
		fputs("buf: 'buffer size' must be at least 2*max('read block, 'write block'),\n"
		      "     to avoid corner case starvations.\n", stderr);
		return -1;
	}
	memset(buf, 0, sizeof *buf);

#ifndef h_mingw
	page = sysconf(_SC_PAGESIZE);
	if (page == hpage) {
		hpage = 0;
	}
	page = hpage ? hpage : page;
	adj = MAX(page, SHMLBA);
#else
	hpage = 0;
	adj = page = 4096;
#endif

	bsiza = ALIGN(bsiz, adj);
	if (!(idx = is_pow2(bsiza))) {
		fprintf(stderr, "buf: 'aligned buffer size' must be power of 2, but is: 0x%zX\n", bsiza);
		goto out;
	}
	buf->mask = (((size_t)1 << idx) - 1);
	rblki = ALIGN(rblk, ioar);
	wblki = ALIGN(wblk, ioaw);
	csiza = ALIGN(rblki + wblki, adj);
	buf->size = bsiza;

	preset_shm(buf);
	if (setup_shm(buf, bsiza, csiza, hpage) < 0) {
		fputs("buf: falling back to regular malloc()\n", stderr);
		ret = 1;
		detach_shm(buf);
		rm_shm(buf);
		preset_mal(buf);
		if (setup_mal(buf, bsiza, csiza, adj) < 0) {
			detach_mal(buf);
			ret = -1;
			goto out;
		}
	} else
		ret = 0;

	buf->rchunk = buf->chk;
	buf->wchunk = buf->chk + rblki;

	buf->flags |= hpage && buf->mapW ? M_HUGE : 0;
	buf->flags |= rline ? M_LINER : 0;
	buf->flags |= wline ? M_LINEW : 0;
//	buf->page = page;
	buf->ioar = ioar;
	buf->ioaw = ioaw;
	buf->rblk = rblk;
	buf->wblk = wblk;
	buf->crcw = crc_beg();
	buf->crcr = buf->crcw;
#if 0
	if (rblk < wblk && rline && !wline) {
		fputs("buf: 'read block' must be greater or equal to 'write block',\n"
		      "     if the left side is in the line mode,\n"
		      "     and the right side is in the reblocking mode.\n", stderr);
		goto out;
	}
#endif

	DEB("\nbuf map: C:%p, B:%p, B+S:%p, W:%p\n", buf->mapC, buf->mapB, buf->mapB + buf->size, buf->mapW);
	DEB("buf buf: C:%p, B:%p\n", buf->chk, buf->buf);
out:
	if (ret < 0)
		buf_dtor(buf);
	fputc('\n', stderr);
	return ret;
}

/*
 * can we read into buffer ?
 * if so, return available space to read into
 */
size_t buf_can_r(struct buf_s *restrict buf)
{
	size_t emp, min;

	emp = (buf->did - buf->got) & buf->mask;
	if unlikely(!emp && !buf->lastr)
		emp = buf->size;
	min = buf->flags & M_LINER ? 1 : buf->rblk;
	/* emp < min => 0 else emp */
	//return emp & ((size_t)(emp < min) - 1);
	if (emp < min)
		emp = 0;
	if (emp > buf->rblk)
		emp = buf->rblk;
	return emp;
}

uint8_t *buf_fetch_r(struct buf_s *restrict buf)
{
	if likely(buf->mapW && ISALI(buf->buf + buf->got, buf->ioar)) {
		/* read will be directly into our buffer */
		buf->fastr = 1;
		return buf->buf + buf->got;
	} else {
		buf->fastr = 0;
		return buf->rchunk;
	}
}

void buf_commit_r(struct buf_s *restrict buf, size_t chunk)
{
	if unlikely(!chunk)
		return;
	buf->allin += chunk;

	if unlikely(!buf->fastr) {
		if likely(buf->mapW || buf->got + chunk <= buf->size)
			memcpy(buf->buf + buf->got, buf->rchunk, chunk);
		else {
			size_t siz1, siz2;
			siz1 = buf->size - buf->got;
			siz2 = chunk - siz1;
			memcpy(buf->buf + buf->got, buf->rchunk, siz1);
			memcpy(buf->buf, buf->rchunk + siz1, siz2);
		}
	} /* else read was directly into buffer */
}

void buf_commit_rf(struct buf_s *restrict buf, size_t chunk)
{
	buf->got = (buf->got + chunk) & buf->mask;
	buf->lastr = 1;
	//buf->flags = (buf->flags & ~(F_FASTR)) | F_LASTR;
}

/*
 * can we write from buffer ?
 * if so, return available space to write from
 */
inline
size_t buf_can_w(struct buf_s * restrict buf)
{
	size_t hav, min;

	hav = (buf->got - buf->did) & buf->mask;
	if unlikely(!hav && buf->lastr)
		hav = buf->size;
	//fprintf(stderr, "buffer: got: %zu did: %zu, hav: %zu, hav%%: %llu\n", buf->got, buf->did, hav, ((uint64_t)hav*100)/buf->size);
	min = buf->flags & M_LINEW ? 1 : buf->wblk;
	/* hav < min => 0 else hav */
	//return hav & ((size_t)(hav < min) - 1);
	if (hav < min)
		hav = 0;
	if (hav > buf->wblk)
		hav = buf->wblk;
	return hav;
}

uint8_t *buf_fetch_w(struct buf_s * restrict buf, size_t chunk)
{
	if likely(buf->mapW && ISALI(buf->buf + buf->did, buf->ioaw)) {
		/* write will be directly from our buffer */
		buf->fastw = 1;
		return buf->buf + buf->did;
	} else {
		buf->fastw = 0;
		if likely(buf->mapW || buf->did + chunk <= buf->size)
			memcpy(buf->wchunk, buf->buf + buf->did, chunk);
		else {
			size_t siz1, siz2;
			siz1 = buf->size - buf->did;
			siz2 = chunk - siz1;
			memcpy(buf->wchunk, buf->buf + buf->did, siz1);
			memcpy(buf->wchunk + siz1, buf->buf, siz2);
		}
		return buf->wchunk;
	}
}

void buf_commit_w(struct buf_s * restrict buf, size_t chunk)
{
	if unlikely(!chunk)
		return;

	buf->allout += chunk;
	/* we only have to calculate crcs here, so simple test is enough */
	if unlikely(!buf->fastw) {
		buf->crcw = crc_calc(buf->crcw, buf->wchunk, chunk);
	} else {
		buf->crcw = crc_calc(buf->crcw, buf->buf + buf->did, chunk);
	}
	buf->crcr = buf->crcw;
}

void buf_commit_wf(struct buf_s *restrict buf, size_t chunk)
{
	buf->did = (buf->did + chunk) & buf->mask;
	buf->lastr = 0;
	//buf->flags = (buf->flags & ~(F_FASTW | F_LASTR));
	//buf->flags &= F_OFF;
}

/*
 * !!!
 * any later call to buf_commit_w() will invalidate crcr
 */
void buf_commit_pad(struct buf_s * restrict buf, const uint8_t *pad, size_t chunk)
{
	buf->allout += chunk;
	buf->crcw = crc_calc(buf->crcw, pad, chunk);
}

/* no more writing, finalize checksums */
static void buf_fincrc(struct buf_s * restrict buf)
{
	const uint8_t *ptr;
	size_t chunk;

	chunk = buf_can_w(buf);
	ptr = buf_fetch_w(buf, chunk);
	buf->crcr = crc_calc(buf->crcr, ptr, chunk);

	buf->cksumr = crc_end(crc_cksum(buf->crcr, buf->allin));
	buf->crcr = crc_end(buf->crcr);

	buf->cksumw = crc_end(crc_cksum(buf->crcw, buf->allout));
	buf->crcw = crc_end(buf->crcw);
}

void buf_rep_stats(struct buf_s * restrict buf)
{
	//static const char warn_nomul[] = "(not a multiple of the block)";
	buf_fincrc(buf);
	fprintf (stderr,
		"Buffer stats:\n"
		"\nread:  %llu (%llu%c blocks)\ncrc:   0x%llX\ncksum: %llu (0x%llX)\n"
		"\nwrote: %llu (%llu%c blocks)\ncrc:   0x%llX\ncksum: %llu (0x%llX)\n",
		buf->allin,  buf->allin / buf->rblk,  buf->allin  % buf->rblk ? '+':' ', (uint64_t)buf->crcr, (uint64_t)buf->cksumr, (uint64_t)buf->cksumr,
		buf->allout, buf->allout / buf->wblk, buf->allout % buf->wblk ? '+':' ', (uint64_t)buf->crcw, (uint64_t)buf->cksumw, (uint64_t)buf->cksumw
	);
}

void buf_rep_setup(struct buf_s * restrict buf)
{
	fprintf (stderr,
		"Buffer setup:\n\n"
		"Size:      %zu\n"
		"Mask:      %zu\n"
//		"Page size: %zu (as accepted by linux)\n"
		"I/O re.a:  %zu\n"
		"I/O wr.a:  %zu\n"
		"Re. block: %zu%s\n"
		"Wr. block: %zu%s\n"
		"SysV shm:  %s\n"
		"HugeTLB:   %s\n",
		buf->size, buf->mask, buf->ioar, buf->ioaw,
		buf->rblk, buf->flags & M_LINER ? " (line mode)" : "",
		buf->wblk, buf->flags & M_LINEW ? " (line mode)" : "",
		buf->mapW ? "yes (hardware wrap-around)" : "no", buf->flags & M_HUGE ? "yes" : "no"
	);
}

void buf_setlinew(struct buf_s *buf)
{
	// buf->flags |= M_LINEW | M_LINER;
	buf->flags |= M_LINEW;
}
#if 0
void buf_setreblock(struct buf_s *buf)
{
	buf->flags &= ~(M_LINEW | M_LINER);
}
#endif
