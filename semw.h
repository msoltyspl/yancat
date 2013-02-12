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
#ifndef __semw_h__
#define __semw_h__

# if   defined(has_sem_posixu)
#  include "semw_posixu.h"
# elif defined(has_sem_posix)
#  include "semw_posix.h"
# elif defined(has_sem_sysv)
#  include "semw_sysv.h"
# elif !defined(has_sem_none)
#  error "Broken config.h ..."
# endif

# if !defined(has_sem_none)

#  define Pb semw_Pb
#  define Vb semw_Vb

int semw_dt(struct sem_s *);

int semw_dtor(struct sem_s *);
int semw_ctor(struct sem_s *, const char *, int, int);

# endif /* no seamphores */

#endif /* header */
