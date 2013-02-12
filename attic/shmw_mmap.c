#if 0
#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#include "common.h"
#include "shmw_mmap.h"

#ifndef MAP_HUGETLB
# ifdef h_linux
#  define MAP_HUGETLB 0x40000
# endif
#endif

#ifndef MAP_ANON
# define MAP_ANON MAP_ANONYMOUS
#endif

int shmw_dt(struct shm_s *s)
{
	if (!s)
		return -1;
	if (s->ptr)
		munmap(s->ptr, s->siz);
	return 0;
}

int shmw_dtorm(struct shm_s **_s)
{
	if (!_s)
		return -1;
	if (shmw_dt(*_s) < 0)
		return -1;
	free(*_s);
	*_s = NULL;
	return 0;
}

#define PROT (PROT_READ | PROT_WRITE)
int shmw_ctorm(struct shm_s **_s, const char *name, size_t *_siz, size_t huge, int circ)
{
	struct shm_s *s = NULL;
	uint8_t *addr = NULL;
	int flags_m = MAP_ANON | MAP_SHARED;
	size_t siz, siz2, page;

	if (!_s || *_s || !name)
		goto out;
	if (!(s = (struct shm_s *)malloc(sizeof *s))) {
		ERRG("malloc()");
		goto out;
	}
	memset(s, 0, sizeof *s);

	page = sysconf(_SC_PAGESIZE);
#ifdef MAP_HUGETLB
	if (huge > page) {
		page = huge;
		flags_m |= MAP_HUGETLB;
# if defined(__ia64__) && defined(h_linux)
		flags_m |= MAP_FIXED;
		addr = (uint8_t *)(0x8000000000000000ULL);
# endif
	}
#else
	if (huge) {
		fputs("error: ANON mmap doesn't know how to use huge pages on this system\n", stderr);
		goto out;
	}
#endif
	siz  = ALIGN(*_siz, page);
	siz2 = siz*(circ ? 2 : 1);

	addr = mmap(addr, siz2, PROT, flags_m, -1, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "error: mmap() '%s': %s\n", name, strerror(errno));
		goto out;
	}
	s->ptr = addr;
	s->siz = siz2;

	if (circ) {
		if (remap_file_pages(addr + siz, siz, 0, 0, flags_m) != 0)
			fprintf(stderr, "warn: remap() '%s': %s\n", name, strerror(errno));
		else
			s->ptrc = addr + siz;
	}
	*_siz = siz;
	*_s = s;
	return 0; /* non-shared allocators (e.g. malloc) return 1 */
out:
	shmw_dtorm(&s);
	return -1;
}

#else
	/* to quiet gcc */
	int no_mmap_shm = 1;
#endif
