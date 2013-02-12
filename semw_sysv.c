/*
 * Copyright 2012 Michal Soltys <soltys@ziu.info>
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
#ifdef has_sem_sysv

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "common.h"
#include "crc.h"
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
	if (s->id >= 0)
		semctl(s->id, 0, IPC_RMID);
	s->id = -1;
	s->name = NULL;
	return 0;
}

int semw_ctor(struct sem_s *s, const char *name, int mp, int val)
{
	int flags = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
	//key_t key;
	unsigned short semw_vals[1] = { [0] = (unsigned short)val };
	union {
		int              val;
		struct semid_ds *buf;
		unsigned short  *array;
		struct seminfo  *__buf;
	} semw_init = { .array = semw_vals };

	if (!s || !name)
		return -1;
	memset(s, 0, sizeof *s);
#if 0
	key = (key_t)crc_str(name);
	key = (key_t)((crc_str(name) & ~0xFFFFu) | (rand() & 0xFFFFu));
#endif
	s->id = semget(IPC_PRIVATE, 1, flags);
	if (s->id < 0) {
		fprintf(stderr, "error: SysV sem '%s': semget(): %s\n", name, strerror(errno));
		return -1;
	}
	if (semctl(s->id, 0, SETALL, semw_init) < 0) {
		fprintf(stderr, "error: SysV sem '%s': semctl(): %s\n", name, strerror(errno));
		goto outid;
	}

	s->name = name;
	return 0;
outid:
	semctl(s->id, 0, IPC_RMID);
	return -1;
}

#else
	/* mostly to quiet gcc */
	int has_no_sysv_semaphores = 1;
#endif
