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
#ifndef __shmw_sysv_h__
#define __shmw_sysv_h__

#include <stdint.h>

struct shm_s {
	int id;
	const char *name;
	uint8_t *ptr, *ptrc;
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
