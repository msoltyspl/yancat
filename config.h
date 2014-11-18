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
#ifndef __config_h__
#define __config_h__
//#define _XOPEN_SOURCE 700
#define _GNU_SOURCE 1

# if   defined(h_linux)

#if 0
#  define has_sem_sysv 1
#  define has_sem_posix 1
#  define has_sem_posixu 1
#  define has_shm_sysv 1
#  define has_shm_posix 1
#  define has_shm_malloc 1
#  define has_mtx_sem 1
#  define has_mtx_posix 1
#endif

#  define has_sem_sysv 1
#  define has_mtx_sem 1
#  define has_shm_sysv 1

# elif defined(h_freebsd)

#  define has_mtx_posix 1
#  define has_sem_posixu 1
#  define has_shm_posix 1
//#  define _BSD_SOURCE 1

# elif defined(h_bsd)

#  define has_mtx_sem 1
#  define has_sem_sysv 1
#  define has_shm_sysv 1
//#  define _BSD_SOURCE 1

# elif defined(h_mingw)

#  define has_mtx_none 1
#  define has_sem_none 1
#  define has_shm_malloc 1

# endif

#if 0
# if defined(has_sem_sysv) || defined (has_shm_sysv)
#  define _SVID_SOURCE 1
# endif
#endif

#endif /* __config_h__ */
