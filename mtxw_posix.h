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
#ifndef __mtxw_posix_h__
#define __mtxw_posix_h__

#include <pthread.h>

struct mtx_s {
	pthread_mutex_t m;
	const char *name;
};

static inline void
mtxw_Vm(struct mtx_s *m)
{
	pthread_mutex_unlock(&m->m);
}

static inline void
mtxw_Pm(struct mtx_s *m)
{
	pthread_mutex_lock(&m->m);
}

#endif
