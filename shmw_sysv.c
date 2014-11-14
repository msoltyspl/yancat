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
#ifdef has_shm_sysv

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "common.h"
#include "crc.h"
#include "shmw.h"

#ifndef SHM_HUGETLB
# ifdef h_linux
#  define SHM_HUGETLB 04000
# endif
#endif

/* Standard attach flags we rely on */
#ifdef SHM_RND
# define AT_FLAGS SHM_RND
#else
# define AT_FLAGS 0
#endif

int shmw_dt(struct shm_s *s)
{
	if (!s)
		return -1;
	if (s->ptrc)
		shmdt(s->ptrc);
	if (s->ptr)
		shmdt(s->ptr);
	/*
	 * we may not NULLize above pointers here, as the area being detached
	 * itself might be in shared area; dtor() is otow final, so clean it up
	 * there
	 */
	return 0;
}

int shmw_dtor(struct shm_s *s)
{
	if (shmw_dt(s) < 0)
		return -1;
	s->ptrc = NULL;
	s->ptr = NULL;
	if (s->id >= 0)
		shmctl(s->id, IPC_RMID, NULL);
	s->id = -1;
	s->name = NULL;
	return 0;
}

int shmw_ctor(struct shm_s *s, const char *name, size_t *_siz, size_t huge, int circ, int sems)
{
	int flags_g = IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR;
	uint8_t *addr = NULL, *addrc = NULL, *addrt;
	size_t siz, page;
	int id, ia64 = 0;

	if (!s || !name)
		return -1;
	memset(s, 0, sizeof *s);

	page = get_page();
#ifndef h_linux
	if (huge) {
		fputs("warn: SysV shm: cannot use HugeTLBfs (non-linux system)\n", stderr);
		return -1;
	}
#else
	if (huge > page) {
		page = huge;
		flags_g |= SHM_HUGETLB;
# ifdef __ia64__
		addr = (uint8_t *)(0x8000000000000000ULL);
		ia64 = 1;
# endif
	}
#endif

	siz = _YALIGN(*_siz, _YMAX(page, (size_t)SHMLBA));
#if 0
	/* this is far from perfect, but ... */
	key = (key_t)((crc_str(name) & ~0xFFFFu) | (rand() & 0xFFFFu));
#endif
	id = shmget(IPC_PRIVATE, siz, flags_g);
	if (id < 0) {
		fprintf(stderr, "error: SysV shm '%s': shmget(): %s\n", name, strerror(errno));
		return -1;
	}
	s->id = id;

	addr = shmat(id, addr, AT_FLAGS);
	if (addr == (void *)-1) {
		fprintf(stderr, "error: SysV shm '%s': shmat(): %s\n", name, strerror(errno));
		goto outid;
	}
	s->ptr = addr;
	if (!circ)
		goto circex;
	/* try above */
	addrc = shmat(id, addr + siz, AT_FLAGS);
	if (addrc != (void *)-1 && (addr + siz == addrc || addrc + siz == addr))
		goto circok;
	if (addrc != (void *)-1)
		shmdt(addrc);
	if (ia64)
		goto circex;
	/* try below */
	addrc = shmat(id, addr - siz, AT_FLAGS);
	if (addrc != (void *)-1 && (addr + siz == addrc || addrc + siz == addr))
		goto circok;
	if (addrc != (void *)-1)
		shmdt(addrc);
	goto circex;
circok:
	if (addrc < addr) {
		addrt = addr;
		addr  = addrc;
		addrc = addrt;
	}
	s->ptr = addr;
	s->ptrc = addrc;
circex:
	*_siz = siz;
	s->name = name;
	return 0; /* non-shared allocators (e.g. malloc) return 1 */
outid:
	shmctl(id, IPC_RMID, NULL);
	return -1;
}

#else
	/* mostly to quiet gcc */
	int has_no_sysv_shm = 1;
#endif
