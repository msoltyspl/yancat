#ifndef __shmw_mmap_h__
#define __shmw_mmap_h__

#include <stdint.h>

struct shm_s {
	uint8_t *ptr, *ptrc;
	size_t siz;
};

static inline uint8_t *
shmw_ptr(const struct shm_s *s)
{
	return s->ptr;
}

static inline int
shmw_cir(const struct shm_s *s)
{
	return s->ptrc != NULL ? 0 : -1;
}

#endif
