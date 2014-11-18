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
#ifndef __semw_posix_h__
#define __semw_posix_h__

#include <semaphore.h>

struct sem_s {
	sem_t *sp;
	char *name;
};

static inline void
semw_Vb(const struct sem_s *s)
{
	sem_post(s->sp);
}

static inline void
semw_Pb(const struct sem_s *s)
{
	int ret;
	do {
		ret = sem_wait(s->sp);
	} while (unlikely(ret < 0) && errno == EINTR);
}

#endif
