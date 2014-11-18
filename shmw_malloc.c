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
#ifdef has_shm_malloc

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "shmw.h"

int shmw_dt(struct shm_s *s)
{
	if (!s)
		return -1;
	return 0;
}

int shmw_dtor(struct shm_s *s)
{
	if (shmw_dt(s) < 0)
		return -1;
	if (s->addr)
		free(s->addr);
	s->addr = NULL;
	return 0;
}

int shmw_ctor(struct shm_s *s, const char *name, size_t *_siz, size_t huge, int circ __attribute__ ((__unused__)), int sems __attribute__ ((__unused__)))
{
	uint8_t *addr = NULL;
	size_t siz, page;

	if (!s || !name)
		return -1;
	if (huge) {
		fputs("error: malloc \"shm\" - cannot use HugeTLBfs\n", stderr);
		return -1;
	}
	memset(s, 0, sizeof *s);

	page = get_page();
	siz = Y_ALIGN(*_siz, page);

	addr = (uint8_t *)malloc(siz + page);
	if (!addr) {
		fprintf(stderr, "error: malloc \"shm\" '%s' failure\n", name);
		return -1;
	}
	s->addr = addr;
	s->ptr = (uint8_t *)Y_ALIGN(addr, page);

	*_siz = siz;
	return 1; /* non-shared allocators (e.g. malloc) return 1 */
}

#else
	/* mostly to quiet gcc */
	int has_no_malloc_shm = 1;
#endif
