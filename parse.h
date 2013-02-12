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
#ifndef __parse_h__
#define __parse_h__

#define SOPT_CNT 16
#define HOST_LEN 128

#define SPEC_IS_TCP 1
#define SPEC_IS_UDP 2

struct nopts_s {
	int opt, lvl, val;
};

struct netpnt_s {
	int dom, copts;
	unsigned short int port;
	struct nopts_s opts[SOPT_CNT];
	char host[HOST_LEN];
};

const char *sp_getsobyint(int opt, int lvl);
int sp_getsobystr(const char *name, int *lvl);
int sp_getvalbystr(const char *name);
void sp_addsobyint(struct nopts_s *opts, int *copts, int opt, int lvl, int val, int ovr);
void sp_delsobyidx(struct nopts_s *opts, int *copts, int idx);
void sp_addsodefs(struct nopts_s *opts, int *copts, int dom);
int sp_addr_parse(struct netpnt_s *a, const char *spec, int dom);
void sp_list_known(void);

#endif
