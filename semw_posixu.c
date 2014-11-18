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
#ifdef has_sem_posixu

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "common.h"
#include "semw.h"

int semw_dt(struct sem_s *s)
{
	if (!s)
		return -1;
	return 0;
}

int semw_dtor(struct sem_s *s)
{
	if (semw_dt(s) < 0)
		return -1;
	if (s->name) {
		sem_destroy(&s->s);
		s->name = NULL;
	}
	return 0;
}

int semw_ctor(struct sem_s *s, const char *name, int mp, int val)
{
	int ret;

	if (!s || !name)
		return -1;
	memset(s, 0, sizeof *s);

	ret = sem_init(&s->s, mp, (unsigned int)val);
	if (ret < 0) {
		fprintf(stderr, "error: POSIX unnamed sem '%s': sem_init(): %s\n", name, strerror(errno));
		return -1;
	}

	s->name = name;
	return 0;
}

#else
	/* mostly to quiet gcc */
	int has_no_posix_unnamed_semaphores = 1;
#endif
