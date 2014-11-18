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
#ifndef __mtxw_h__
#define __mtxw_h__

# if   defined(has_mtx_posix)
#  include "mtxw_posix.h"
# elif defined(has_mtx_sem)
#  include "semw.h"
# elif !defined(has_mtx_none)
#  error "Broken config.h ..."
# endif

# if defined(has_mtx_posix)

#  define Pm mtxw_Pm
#  define Vm mtxw_Vm

int mtxw_dt(struct mtx_s *);

int mtxw_dtor(struct mtx_s *);
int mtxw_ctor(struct mtx_s *, const char *, int);

# elif defined(has_mtx_sem)

#  define mtx_s sem_s
#  define Pm semw_Pb
#  define Vm semw_Vb

#define mtxw_dtor semw_dtor
#define mtxw_dt semw_dt

static inline
int mtxw_ctor(struct mtx_s *s, const char *n, int mp)
{
	return semw_ctor(s, n, mp, 1);
}

# endif

#endif /* header */
