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
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
// #include <string.h>

#include "crc.h"

const char err_generic[] = "%s failure @%s:%d\n";

unsigned long int get_ul(const char *s)
{
	char *tail;
	unsigned long int x;

	errno = 0;
	if (!s)
		return 0;
	x = strtoul(s, &tail, 10);
	if (errno)
		return 0;
	switch (*tail) {
		case ':':
		case ',':
		case '\0':
		case 'b':
		case 'B':
			break;
		case 'K':
			x *= 1000ul;
			break;
		case 'k':
			x <<= 10;
			break;
		case 'M':
			x *= 1000000ul;
			break;
		case 'm':
			x <<= 20;
			break;
		case 'G':
			x *= 1000000000ul;
			break;
		case 'g':
			x <<= 30;
			break;
		default:
			errno = EINVAL;
			return 0;
	}
	return x;
}

double get_double(const char *s)
{
	char *tail;
	double x;

	errno = 0;
	if (!s)
		return 0;
	x = strtod(s, &tail);
	if (errno)
		return 0;
	if (*tail) {
		errno = EINVAL;
		return 0;
	}
	return x;
}

/* returns bit index, starting with 0 */
int is_pow2(size_t x)
{
	size_t y = 1;
	int cnt = 0, pos = 0, is, posr = 0;

	while (y) {
		is = (x & y) > 0;
		if (is) {
			cnt++;
			posr = pos;
		}
		y <<= 1;
		pos++;
	}
	return cnt == 1 ? posr : posr | INT_MIN;
}

void get_strrnd(char *restrict s, int len) {
	static const char a[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890";

	while (len) {
		s[--len] = a[rand() % (int)(sizeof(a) - 1)];
	}
}

size_t get_page(void)
{
#ifndef h_mingw
	long int page;

	page = sysconf(_SC_PAGESIZE);
	if (page < 0)
		return 4096;
	return (size_t)page;
#else
	return 4096;
#endif
}

size_t get_pathmax(const char *ptr __attribute__ ((__unused__)))
{
#ifndef h_mingw
	long int pathmax;

	errno = 0;
	pathmax = pathconf(ptr ? ptr : "/", _PC_PATH_MAX);
	if (pathmax < 0 && errno)
		return 256;
	if (pathmax < 0 || pathmax > 4096)
		return 4096;
	return (size_t)pathmax;
#else
	return 260;
#endif
}

int common_init(void)
{
	crc_init();
	srand ((unsigned int)time(0));
	return 0;
}

/* strerror_r wrapper to cover its possible error (XSI only) */
/*
void strerror_rw(int e, char *buf, size_t s)
{
	char ext[256];
	size_t len;
	int ret;
	if ((ret = strerror_r(e, buf, s)) < 0) {
		switch (ret) {
			case EINVAL:
				strcpy(ext, "[strerror_r(): invalid errno value]"); break;
			case ERANGE:
				strcpy(ext, "[strerror_r(): not enough space in buffer to describe error]"); break;
			default:
				strcpy(ext, "[strerror_r(): wtf ?]");
		}
		len = Y_MIN(s, strlen(ext) + 1);
		memcpy(buf, ext, len);
		buf[len - 1] = 0;
	}
}
*/

#if 0
int get_ul(size_t *v, const char *s)
{
	return __get_int(v, s, strtoul);
}

int get_sl(ssize_t *v, const char *s)
{
	return __get_int((size_t *)v, s, (unsigned long int (*)(const char *, char **, int))strtol);
}
#endif
