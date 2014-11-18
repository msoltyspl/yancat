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
#ifdef has_sem_posix

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
	if (s->sp)
		sem_close(s->sp);
	/*
	 * we may not NULLize above pointer here, as the area being detached
	 * itself might be in shared area; dtor() is otow final, so clean it up
	 * there
	 */
	return 0;
}

int semw_dtor(struct sem_s *s)
{
	if (semw_dt(s) < 0)
		return -1;
	s->sp = NULL;
	if (s->name) {
		sem_unlink(s->name);
		free(s->name);
		s->name = NULL;
	}
	return 0;
}

static char* get_rndname(const char* f)
{
	size_t pathmax, len;
	char *point = NULL;

	/* mandated by shm_open portability (supposedly ?) */
	pathmax = 255;
	len = strlen(f) + 9;
	if (len >= pathmax) {
		ERRG("insane path!");
		goto out;
	}
	if (!(point = (char *)malloc(len + 1))) {
		ERRG("malloc()");
		goto out;
	}

	strcpy(point, f);
	point[len - 9] = '-';
	get_strrnd(point + len - 8, 8);
	point[len] = '\0';
out:
	return point;
}

int semw_ctor(struct sem_s *s, const char *name, int mp, int val)
{
	if (!s || !name)
		return -1;
	memset(s, 0, sizeof *s);

	s->name = get_rndname(name);
	if (!s->name)
		return -1;

	s->sp = sem_open(s->name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, val);
	if (!s->sp) {
		fprintf(stderr, "error: POSIX sem '%s': sem_open(): %s\n", name, strerror(errno));
		goto outnme;
	}
	return 0;
outnme:
	free(s->name);
	return -1;
}

#else
	/* mostly to quiet gcc */
	int has_no_posix_semaphores = 1;
#endif
