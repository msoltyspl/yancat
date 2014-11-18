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
#ifdef has_mtx_posix

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "common.h"
#include "mtxw.h"

int mtxw_dt(struct mtx_s *m)
{
	if (!m)
		return -1;
	return 0;
}

int mtxw_dtor(struct mtx_s *m)
{
	if (mtxw_dt(m) < 0)
		return -1;
	if (m->name) {
		pthread_mutex_destroy(&m->m);
		m->name = NULL;
	}
	return 0;
}

int mtxw_ctor(struct mtx_s *m, const char *name, int mp)
{
	int ret;
	pthread_mutexattr_t attr;

	if (!m || !name)
		return -1;
	memset(m, 0, sizeof *m);

	if ((ret = pthread_mutexattr_init(&attr))) {
		fprintf(stderr, "pthread_mutexattr_init(): %s\n", strerror(ret));
		return -1;
	}
	if ((ret = pthread_mutexattr_setpshared(&attr, mp ? PTHREAD_PROCESS_SHARED : PTHREAD_PROCESS_PRIVATE))) {
		fprintf(stderr, "pthread_mutexattr_setpshared(): %s\n", strerror(ret));
		goto outat;
	}
	if ((ret = pthread_mutex_init(&m->m, &attr))) {
		fprintf(stderr, "pthread_mutex_init(): %s\n", strerror(ret));
		goto outat;
	}

	m->name = name;
	ret = 0;
outat:
	pthread_mutexattr_destroy(&attr);
	return ret ? -1 : 0;
}

#else
	/* mostly to quiet gcc */
	int has_no_posix_mutexes = 1;
#endif
