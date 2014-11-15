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
#ifndef __common_h__
#define __common_h__

#include <stdint.h>

/*
 * in order: cpu+comp barrier, comp barrier, variable barrier
 */
#define full_barrier() __sync_synchronize()
#define barrier() __asm__ __volatile__("":::"memory")
/* #define var_barrier(x) __asm__ __volatile__("":"=m"(x):"m"(x)) */
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#define cmpxchg(x,o,n) __sync_bool_compare_and_swap(&(x),(o),(n));

#define Y_MAX(x,y) ((x) > (y) ? (x) : (y))
#define Y_MIN(x,y) ((x) < (y) ? (x) : (y))
#define Y_ALIGN(x, a) (((uintptr_t)(x) + a - 1) & ~((uintptr_t)(a) - 1))
#define Y_ISALI(x, a) (((uintptr_t)(x) & ~((uintptr_t)(a) - 1)) == 0)

// hmmm ...
#if PROFILE > 0
# define likely(x)      (x)
# define unlikely(x)    (x)
#else
# define likely(x)      (__builtin_expect(!!(x), 1))
# define unlikely(x)    (__builtin_expect(!!(x), 0))
#endif

#if DEBUG == 1
# define DEB(...) fprintf(stderr, __VA_ARGS__)
#else
# define DEB(...) ((void)(0))
#endif

#define ERRG(x) fprintf(stderr, err_generic, (x), __FILE__, __LINE__)

unsigned long int get_ul(const char *s);
double get_double(const char *s);
int is_pow2(size_t x);
void get_strrnd(char *restrict s, int len);
size_t get_page(void);
size_t get_pathmax(const char *ptr);
int common_init(void);

extern const char err_generic[];

#endif
