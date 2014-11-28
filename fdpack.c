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
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <signal.h>
#ifndef h_mingw
# include <sys/socket.h>
# include <netinet/in_systm.h>
# include <netinet/in.h>
# include <netinet/ip.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <netdb.h>
# define setsockopt_wr setsockopt
# define _O_BINARY 0
#else
# include <winsock2.h>
# define S_IRGRP 0
# define S_IROTH 0
/* unsigned int _CRT_fmode = _O_BINARY; */
# define setsockopt_wr(a,b,c,d,e) setsockopt(a,b,c,(const char*)d,e)
# define strerror_r(a,b,c) strerror(a)
# ifndef MSG_WAITALL
#  if _WIN32_WINNT >= 0x0502
#   define MSG_WAITALL 0x8
#  else
#   define MSG_WAITALL 0
#  endif
# endif
#endif

#ifndef O_LARGEFILE
# define O_LARGEFILE 0
#endif
#ifndef MSG_NOSIGNAL
# define MSG_NOSIGNAL 0
#endif

#include "common.h"
#include "parse.h"
#include "fdpack.h"

static void fd_info_(struct fdpack_s*);
static void fd_info_f(struct fdpack_s*);
static void fd_info_s(struct fdpack_s*);
static int fd_open_(struct fdpack_s*);
static int fd_open_f(struct fdpack_s*);
static int fd_open_s(struct fdpack_s*);
static int fd_close_(struct fdpack_s*);
static int fd_close_s(struct fdpack_s*);
static int fd_dtor_(struct fdpack_s*);
static int fd_dtor_f(struct fdpack_s*);
static ssize_t fd_read_(struct fdpack_s *, void *, size_t);
static ssize_t fd_read_s(struct fdpack_s *, void *, size_t);
static ssize_t fd_write_(struct fdpack_s *, const void *, size_t);
static ssize_t fd_write_s(struct fdpack_s *, const void *, size_t);

const struct fdtype_s _fdfd = {
		.kind = "fd",
		.dtor = &fd_dtor_,
		.open = &fd_open_,
		.close = &fd_close_,
		.read = &fd_read_,
		.write = &fd_write_,
		.info = &fd_info_,
};
const struct fdtype_s _fdfile = {
		.kind = "file",
		.dtor = &fd_dtor_f,
		.open = &fd_open_f,
		.close = &fd_close_,
		.read = &fd_read_,
		.write = &fd_write_,
		.info = &fd_info_f,
};

const struct fdtype_s _fdsock = {
		.kind = "socket",
		.dtor = &fd_dtor_,
		.open = &fd_open_s,
		.close = &fd_close_s,
		.read = &fd_read_s,
		.write = &fd_write_s,
		.info = &fd_info_s,
};

/*
 * virtuals
 */
static void
fd_info_(struct fdpack_s *fd)
{
	fprintf(stderr,"  type:  %s\n  dir:   %s\n  fd:    %d\n",
		fd->type->kind,
		fd->dir ? "output" : "input",
		fd->fd
       );
}
static void
fd_info_f(struct fdpack_s *fd)
{
	fd_info_(fd);
	fprintf(stderr,"  path:  %s\n", fd->f.path);
}

static void
fd_info_s(struct fdpack_s *fd)
{
	const char *str;
	int i, m;

	fd_info_(fd);
	if (!(str = inet_ntoa(fd->s.saddr.sin_addr)))
		str = "?";
	fprintf(stderr,"  host:  %s (%s)\n  port:  %hu\n  proto: %d\n  options (%d):\n",
		fd->s.np.host,
		str,
		fd->s.np.port,
		/* ntohs(fd->s.saddr.sin_port), */
		fd->s.np.dom,
		fd->s.np.copts
	);
	m = 0;
	for (i = 0; i < fd->s.np.copts; i++) {
		str = sp_getsobyint(fd->s.np.opts[i].opt, fd->s.np.opts[i].lvl);
		if (!str)
			continue;
		if (m)
			fputs(", ", stderr);
		else
			fputs("   ", stderr);
		fprintf(stderr,"%s=%d", str, fd->s.np.opts[i].val);
		m = 1;
	}
	if (m)
		fputc('\n', stderr);
}

static int
fd_close_(struct fdpack_s *fd)
{
	int ret = -1;
	if (fd->fd >= 0) {
		if (fd->sync)
#ifdef h_mingw
			_commit(fd->fd);
#else
			fsync(fd->fd);
#endif
		ret = TFR(close(fd->fd));
	} else
		ret = 0;
	fd->fd = -1;
	return ret;
}

static int
fd_close_s(struct fdpack_s *fd)
{
	int ret = -1;
	if (fd->fd >= 0) {
#ifdef h_mingw
		/*
		 * shutdown() is supposedly needed for graceful termination on
		 * windows; OTOH, some sources claim that closesocket() does so
		 * implicitly;
		 */
		shutdown(fd->fd, SD_BOTH);
		ret = closesocket(fd->fd);
#else
		ret = TFR(close(fd->fd));
#endif
	} else
		ret = 0;
	fd->fd = -1;
	return ret;
}

static int
fd_dtor_(struct fdpack_s *fd)
{
	return fd_close(fd);
}

static int
fd_dtor_f(struct fdpack_s *fd)
{
	if (fd->f.path)
		free(fd->f.path);
	fd->f.path = NULL;
	return fd_dtor_(fd);
}

static ssize_t
fd_read_(struct fdpack_s *fd, void *buf, size_t count)
{
	return read(fd->fd, buf, count);
}

static ssize_t
fd_read_s(struct fdpack_s *fd, void *buf, size_t count)
{
	return recv(fd->fd, buf, count, fd->s.flags);
}

static ssize_t
fd_write_(struct fdpack_s *fd, const void *buf, size_t count)
{
	return write(fd->fd, buf, count);
}

static ssize_t
fd_write_s(struct fdpack_s *fd, const void *buf, size_t count)
{
	return send(fd->fd, buf, count, MSG_NOSIGNAL);
}

static int
fd_open_(struct fdpack_s* fd)
{
	/* note: direct file descriptor can't be reopened */
	if (!fd || fd->fd < 0)
		return -1;
	return 0;
}

static int
fd_open_f(struct fdpack_s* fd)
{
	int mode, flags, ret = -1;

	if (!fd || fd->fd != -1)
		goto out;
	if (fd->dir) {
		mode = O_WRONLY | O_TRUNC | O_CREAT;
		flags = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	} else {
		mode = O_RDONLY;
		flags = 0;
	}
	mode |= _O_BINARY | O_LARGEFILE;

	ret = open(fd->f.path, mode, flags);
	if (ret < 0) {
		perror("open()");
		goto out;
	}

	fd->fd = ret;
	ret = 0;
out:
	return ret;
}

static int
fd_setup_socket(struct fdpack_s* fd)
{
	int i, fdo, ret;
	char errinfo[256];

	fdo = socket(PF_INET, fd->s.np.dom == IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM, fd->s.np.dom);
	if (fdo < 0) {
		perror("socket()");
		return -1;
	}
	for (i = 0; i < fd->s.np.copts; i++) {
		ret = setsockopt_wr(fdo, fd->s.np.opts[i].lvl, fd->s.np.opts[i].opt, &fd->s.np.opts[i].val, sizeof fd->s.np.opts[i].val);
		if (ret < 0) {
			fprintf(stderr, "Unable to set socket option %s: %s", sp_getsobyint(fd->s.np.opts[i].opt, fd->s.np.opts[i].lvl), strerror_r(errno, errinfo, sizeof errinfo));
			close(fdo);
			return -1;
			/* sp_delsobyidx(np->opts, &np->copts, i--); */
		}
	}
	return fdo;
}


static int
fd_open_s(struct fdpack_s* fd)
{
	struct sockaddr saddr_alias;
	int fdo, ret = -1;
#ifndef h_mingw
	char errinfo[256];
#endif

	if (!fd || fd->fd != -1)
		return -1;

	fdo = fd_setup_socket(fd);
	if (fdo < 0)
		return -1;

	if (fd->dir) {
		/* sending to ... */
		/* avoid aliasing error, though this is false positive (at high warning levels) */
		/* TODO: handle EINTR from connect() that makes it finish asynchronously (select) */
		memcpy(&saddr_alias, &fd->s.saddr, sizeof fd->s.saddr);
		ret = connect(fdo, &saddr_alias, sizeof fd->s.saddr);
		if (ret < 0) {
			fprintf(stderr, "Unable to connect to %s: %s", fd->s.np.host, strerror_r(errno, errinfo, sizeof errinfo));
			goto out;
		}
	} else {
		/* receiving from ... */
		/* avoid aliasing error, though this is false positive (at high warning levels) */
		memcpy(&saddr_alias, &fd->s.saddr, sizeof fd->s.saddr);
		ret = bind(fdo, &saddr_alias, sizeof fd->s.saddr);
		if (ret < 0) {
			perror("bind()");
			goto out;
		}
		if (fd->s.np.dom == IPPROTO_TCP) {
			ret = listen(fdo, 1);
			if (ret < 0) {
				perror("listen()");
				goto out;
			}
			ret = accept(fdo, NULL, NULL);
			if (ret < 0) {
				perror("accept()");
				goto out;
			}
			TFR(close(fdo));
			fdo = ret;
		}
	}

	fd->fd = fdo;
	return 0;
out:
	TFR(close(fdo));
	return -1;
}

/*
 * regular
 */

int fd_ctor(struct fdpack_s* fd, int dir, const char *fdstr, int sync)
{
	int fdnbr;

	if (!fd || !fdstr)
		goto out;

	fdnbr = (int)get_ul(fdstr);
	if (errno) {
		fprintf(stderr, "Bad fd number: %s\n", fdstr);
		goto out;
	}

#ifndef h_mingw
	int ret = TFR(fcntl(fdnbr, F_GETFL));
	if (ret < 0) {
		fprintf(stderr, "File descriptor %d is invalid.\n", fdnbr);
		goto out;
	}
	if (dir && (ret & O_ACCMODE) == O_RDONLY) {
		fprintf(stderr, "Direction is 'out', but fd %d is not opened for writing.\n", fdnbr);
		goto out;
	}
	if (!dir && (ret & O_ACCMODE) == O_WRONLY) {
		fprintf(stderr, "Direction is 'in', but fd %d is not opened for reading.\n", fdnbr);
		goto out;
	}
#endif
#ifdef h_mingw
	_setmode(fdnbr, _O_BINARY);
#endif
	fd->type = &_fdfd;
	fd->fd = fdnbr;
	fd->dir = dir;
	fd->sync = sync && dir;
	return 0;
out:
	return -1;
}

int fd_ctor_f(struct fdpack_s* fd, int dir, const char *path, int sync)
{
	size_t len, pathmax;

	if (!fd || !path)
		goto out;

	pathmax = get_pathmax(path);
	len = strlen(path);
	if (len >= (size_t)pathmax)
		goto out;

	fd->f.path = (char *)malloc(len + 1);
	if (!fd->f.path) {
		ERRG("malloc()");
		goto out;
	}

	fd->type = &_fdfile;
	strcpy(fd->f.path, path);
	fd->fd = -1;
	fd->dir = dir;
	fd->sync = sync && dir;

	return 0;
out:
	return -1;
}

int fd_ctor_s(struct fdpack_s* fd, int dir, struct netpnt_s *np, int msgwait)
{
	struct sockaddr_in saddr;
	struct hostent *hent;

	if (!fd || !np || !np->dom)
		return -1;

	memset(&saddr, 0, sizeof saddr);

	/* TODO switch to getaddrinfo() ? */
	if (*np->host && strcmp(np->host, "*")) {
		if (!(hent = gethostbyname(np->host)) ||
		      hent->h_length != sizeof saddr.sin_addr.s_addr ||
		      hent->h_addrtype != AF_INET) {
			fprintf(stderr, "Can't find the ipv4 address for the host: %s\n", np->host);
			return -1;
		}
		memcpy(&(saddr.sin_addr.s_addr), hent->h_addr_list[0], (size_t)hent->h_length);
	} else {
		if (dir) {
			fprintf(stderr, "Cannot send to \"any\".\n");
			return -1;
		}
		saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	}
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(np->port);

	fd->type = &_fdsock;
	fd->fd = -1;
	fd->dir = dir;
	fd->sync = 0;
	if (dir)
		fd->s.flags = 0;
	else
		fd->s.flags = msgwait ? MSG_WAITALL : 0;
	memcpy(&fd->s.saddr, &saddr, sizeof saddr);
	memcpy(&fd->s.np, np, sizeof *np);
	return 0;
}
