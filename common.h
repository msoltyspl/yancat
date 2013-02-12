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
#define __fmb() __sync_synchronize()
#define __cmb()  __asm__ __volatile__("":::"memory")
#define __cvmb(x) __asm__ __volatile__("":"=m"(x):"m"(x))

#define __cmpxchg(x,o,n) __sync_bool_compare_and_swap(&(x),(o),(n));

/* so ugly ... */
#if 0
#define acc_r(t,n) static inline const t n(const t * restrict _d) { return *(const volatile t *)_d; }
#define acc_w(t,n) static inline void    n(      t * restrict _d, t v) { *(volatile t *)_d  = v; }
acc_r(sig_atomic_t, _vr)
acc_w(sig_atomic_t, _vw)
#endif

#define _YMAX(x,y) ((x) > (y) ? (x) : (y))
#define _YMIN(x,y) ((x) < (y) ? (x) : (y))
#define _YALIGN(x, a) (((uintptr_t)(x) + a - 1) & ~((uintptr_t)(a) - 1))
#define _YISALI(x, a) (((uintptr_t)(x) & ~((uintptr_t)(a) - 1)) == 0)

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
