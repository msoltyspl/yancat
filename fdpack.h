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
#ifndef __fdpack_h__
#define __fdpack_h__

#ifndef h_mingw
# include <netinet/in.h>
#else
# include <winsock2.h>
#endif

#include "parse.h"

struct fdtype_s;
struct fdpack_s;

struct fdtype_s {
	const char *kind;
	void (*info)(struct fdpack_s *);
	int (*open)(struct fdpack_s*);
	int (*close)(struct fdpack_s*);
	int (*dtor)(struct fdpack_s*);
	ssize_t (*read)(struct fdpack_s *, void *, size_t);
	ssize_t (*write)(struct fdpack_s *, const void *, size_t);
};

struct fdpack_s {
	const struct fdtype_s *type;
	int fd, dir, sync;
	union {
		struct {
			char *path;
		} f;
		struct {
			struct sockaddr_in saddr;
			struct netpnt_s info;
			int flags;
		} s;
	};
};

extern const struct fdtype_s _fdfd;
extern const struct fdtype_s _fdfile;
extern const struct fdtype_s _fdsock;

#if 0
void fd_info(struct fdpack_s *fd);
ssize_t fd_read(struct fdpack_s *fd, void *buf, size_t count);
ssize_t fd_write(struct fdpack_s *fd, const void *buf, size_t count);
int fd_open(struct fdpack_s *fd);
int fd_dtor(struct fdpack_s *fd);
#endif

int fd_ctor  (struct fdpack_s* fd, int dir, const char *sd, int sync);
int fd_ctor_f(struct fdpack_s* fd, int dir, const char *path, int sync);
int fd_ctor_s(struct fdpack_s* fd, int dir, struct netpnt_s *a, int msgwait);

/*
 * virtuals
 */

static inline void
fd_info(struct fdpack_s *fd)
{
	fd->type->info(fd);
}

static inline ssize_t
fd_read(struct fdpack_s *fd, void *buf, size_t count)
{
	return fd->type->read(fd, buf, count);
}

static inline ssize_t
fd_write(struct fdpack_s *fd, const void *buf, size_t count)
{
	return fd->type->write(fd, buf, count);
}

static inline int
fd_open(struct fdpack_s *fd)
{
	return fd->type->open(fd);
}

static inline int
fd_close(struct fdpack_s *fd)
{
	return fd->type->close(fd);
}

static inline int
fd_dtor(struct fdpack_s *fd)
{
	if (!fd)
		return -1;
	return fd->type->dtor(fd);
}

#endif
