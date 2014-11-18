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
#ifndef __semw_sysv_h__
#define __semw_sysv_h__

#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

struct sem_s {
	int id;
	const char *name;
};

static inline void
semw_Vb(const struct sem_s *s)
{
	static struct sembuf op = { .sem_flg = 0, .sem_num = 0, .sem_op = +1 };
	int ret;
	do {
		ret = semop(s->id, &op, 1);
	} while (unlikely(ret < 0) && errno == EINTR);
}

static inline void
semw_Pb(const struct sem_s *s)
{
	static struct sembuf op = { .sem_flg = 0, .sem_num = 0, .sem_op = -1 };
	int ret;
	do {
		ret = semop(s->id, &op, 1);
	} while (unlikely(ret < 0) && errno == EINTR);
}

#endif
