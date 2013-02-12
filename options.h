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
#ifndef __options_h__
#define __options_h__

#include "parse.h"

struct options_s {
	const char *fd[2];
	const char *file[2];
	struct netpnt_s sock[2];
	size_t bsiz;
	size_t rblk, rcnt;
	size_t wblk, wcnt;
	size_t rsp, wsp;
	size_t hpage;
	int fsync, strict, rline, wline, rcrc, wcrc, onetask, thread, loop, cpuM, cpuS;
};

int  opt_parse(struct options_s*, int argc, char **argv);

#endif
