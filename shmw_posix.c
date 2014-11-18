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
#ifdef has_shm_posix

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <mntent.h>

#include "common.h"
#include "shmw.h"

int shmw_dt(struct shm_s *s)
{
	if (!s)
		return -1;
	if (s->ptrc)
		munmap(s->ptrc, s->siz);
	if (s->ptr)
		munmap(s->ptr, s->siz);
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
	if (s->name) {
		if (s->ishuge)
			unlink(s->name);
		else
			shm_unlink(s->name);
		free(s->name);
		s->name = NULL;
	}
	return 0;
}

/* find hugetlbfs mount, randomize, use if path length is sane */
static char* get_hmnt(const char* f)
{
	size_t pathmax, len;
	char *point = NULL;
	struct mntent *m;
	FILE *fs = setmntent("/etc/mtab", "r");

	if (!fs)
		return NULL;

	while ((m = getmntent(fs))) {
		if (strcmp(m->mnt_type, "hugetlbfs"))
			continue;
		fprintf(stderr, "info: HugeTLBfs found at %s\n", m->mnt_dir);
		pathmax = get_pathmax(m->mnt_dir);
		len = strlen(m->mnt_dir) + strlen(f) + 9;
		if (len >= pathmax) {
			ERRG("insane path!");
			break;
		}
		if (!(point = (char *)malloc(len + 1))) {
			ERRG("malloc()");
			break;
		}

		strcpy(point, m->mnt_dir);
		strcat(point, f);
		point[len - 9] = '-';
		get_strrnd(point + len - 8, 8);
		point[len] = '\0';
		break;
	}
	if (!m)
		fputs("error: HugeTLBfs mount no found\n", stderr);
	endmntent(fs);
	return point;
}

static char* get_smnt(const char* f)
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

#define PROT (PROT_READ | PROT_WRITE)
#ifndef MAP_HASSEMAPHORE
# define MAP_HASSEMAPHORE 0
#endif
#ifndef MAP_NOSYNC
# define MAP_NOSYNC 0
#endif

int shmw_ctor(struct shm_s *s, const char *name, size_t *_siz, size_t huge, int circ, int sems)
{
	uint8_t *addr = NULL, *addrc = NULL, *addrt;
	int flags_m = MAP_SHARED | MAP_NOSYNC | (sems ? MAP_HASSEMAPHORE : 0);
	size_t siz, page;
	int fd = -1, iaflag = 0;

	if (!s || !name)
		return -1;
	memset(s, 0, sizeof *s);

	page = get_page();
#ifndef h_linux
	if (huge) {
		fputs("warn: POSIX shm: cannot use HugeTLBfs (non-linux system)\n", stderr);
		return -1;
	}
#endif

	if (huge > page) {
		page = huge;
#ifdef __ia64__
		flags_m |= MAP_FIXED;
		addr = (uint8_t *)(0x8000000000000000ULL);
		iaflag = 1;
#endif
		s->ishuge = 1;
		s->name = get_hmnt(name);
	} else {
		s->name = get_smnt(name);
	}
	if (!s->name)
		return -1;
	siz = Y_ALIGN(*_siz, page);
	s->siz = siz;

	if (s->ishuge)
		fd = open(s->name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
	else
		fd = shm_open(s->name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);

	if (fd < 0) {
		fprintf(stderr, "error: POSIX shm '%s': open(): %s\n", name, strerror(errno));
		goto outstr;
	}

	if (ftruncate(fd, (off_t)siz) < 0) {
		fprintf(stderr, "error: POSIX shm '%s': ftruncate(): %s\n", name, strerror(errno));
		goto outfd;
	}

	addr = mmap(addr, siz, PROT, flags_m, fd, 0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "error: POSIX shm '%s': mmap(): %s\n", name, strerror(errno));
		goto outfd;
	}
	s->ptr = addr;
	if (!circ)
		goto circex;
	/* try above */
	addrc = mmap(addr + siz, siz, PROT, flags_m, fd, 0);
	if (addrc != MAP_FAILED && (addr + siz == addrc || addrc + siz == addr))
		goto circok;
	if (addrc != MAP_FAILED)
		munmap(addrc, siz);
	if (iaflag)
		goto circex;
	/* try below */
	addrc = mmap(addr - siz, siz, PROT, flags_m, fd, 0);
	if (addrc != MAP_FAILED && (addr + siz == addrc || addrc + siz == addr))
		goto circok;
	if (addrc != MAP_FAILED)
		munmap(addrc, siz);
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
	close(fd);
	*_siz = siz;
	return 0; /* non-shared allocators (e.g. malloc) return 1 */
outfd:
	if (s->ishuge)
		unlink(s->name);
	else
		shm_unlink(s->name);
	close(fd);
outstr:
	free(s->name);
	return -1;
}

#else
	/* mostly to quiet gcc */
	int has_no_posix_shm = 1;
#endif
