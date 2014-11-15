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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <getopt.h>
#include <errno.h>
#ifdef h_affi
# include <sched.h>
#endif

#include "version.h"
#include "common.h"
#include "options.h"
#include "parse.h"

#define DEF_MAXCNT 1048576u
#define DEF_MAXBLK 4194304u
#define DEF_MAXHPAGE (256u*1048576u)


static void unset_in(struct options_s *opts)
{
	opts->fd[0] = NULL;
	opts->file[0] = NULL;
	opts->sock[0].dom = 0;
}

static void unset_out(struct options_s *opts)
{
	opts->fd[1] = NULL;
	opts->file[1] = NULL;
	opts->sock[1].dom = 0;
}

static void set_default(struct options_s *opts)
{
	memset(opts, 0, sizeof *opts);

	opts->bsiz = 4194304;
	opts->rblk = 65536;
	opts->wblk = 65536;
	opts->rcnt = 1;
	opts->wcnt = 1;
	opts->cpuR = -1;
	opts->cpuW = -1;
#ifdef h_mingw
	opts->mode = sp;
#else
	opts->mode = mp;
#endif
}

static void help(void)
{
	fprintf(stderr,
		"\n"
		"Yancat\n"
		YANVER "\n"
		"(c) 2012 Michal Soltys\n"
		"\n"
		"	-i <spec>	input fd specification\n"
		"	-o <spec>	output fd specification\n"
		"	-b <size>	reader's blocking unit\n"
		"	-B <size>	writer's blocking unit\n"
		"	-m <size>	buffer size\n"
		"	-n <size>	try to read at least <n> blocks (n/a if MP)\n"
		"	-N <size>	try to write at least <n> blocks (n/a if MP)\n"
		"	-H <size>	request huge pages of size <n> (linux only)\n"
		"	-p <float>	stall point after overrun (reader)\n"
		"	-P <float>	stall point after underrun (writer)\n"
#ifdef h_affi
		"	-u <cpu>	try to run reader only on <cpu>\n"
		"	-U <cpu>	try to run writer only on <cpu>\n"
#endif
#ifdef h_thr
		"	-t	use posix threads instead of processes\n"
#endif
#ifndef h_mingw
		"	-1	use single process\n"
#endif
		"	-y	fsync output after the transfer\n"
		"	-r	strict blocking writes\n"
		"	-l	reader in byte/line mode\n"
		"	-L	writer in byte/line mode\n"
		"	-g	enable builtin looping until error/interruption\n"
		"	-c	calculate crc & cksum (reader)\n"
		"	-C	calculate crc & cksum (writer)\n"
		"	-h	help + known socket options\n"
		"\n"
		"- <spec> is ([fdtu]:|:|)<string> (case insensitive), eg:\n"
		"	t:host.tld:12345:SO_SNDBUF=67890,TCP_NODELAY,IPTOS=1|2|IPTOS_LOWDELAY\n"
		"	u:host.tld:12345:SO_SNDBUF=1472\n"
		"	d:123\n"
		"	-\n"
		"	f:/stuff/backup.tar\n"
		"	'd' and 'f:' can usually be omitted; '-' substitutes for d:0 or d:1\n"
		"- <size> is an integer, which can be suffixed with [bBkKmMgG]\n"
		"- <cpu> is a required cpu number, starting with 0\n"
		"- <float> in -[pP] is a value >0.0 <1.0, constrained by the block sizes\n"
		"- if no input and/or output is provided, sdtin/stdout are used\n\n"
		"- see README for additional info\n"
		"\n"
	);
	sp_list_known();
}

static int
opt_subparse(struct options_s *opts, const char *spec, int dir)
{
	int ret = -1;
	size_t len, len2;
	static const char* dash[] = { "0", "1" };

	if (!spec)
		return -1;

	len = strlen(spec);
	len2 = len > 2 ? 2 : len;

	if (!strncasecmp(spec, "t:", len2)) {
		ret = sp_addr_parse(&opts->sock[dir], spec + 2, SPEC_IS_TCP);
	} else if (!strncasecmp(spec, "u:", len2)) {
		ret = sp_addr_parse(&opts->sock[dir], spec + 2, SPEC_IS_UDP);
	} else if (*spec == ':' || !strncasecmp(spec, "d:", len2)) {
		spec++;
		if (*spec == ':')
			spec++;
		opts->fd[dir] = spec;
		ret = 0;
	} else if (!strcasecmp(spec, "-")) {
		opts->fd[dir] = dash[dir];
		ret = 0;
	} else {
		if (!strncasecmp(spec, "f:", len2))
			spec += 2;
		opts->file[dir] = spec;
		ret = 0;
	}

	return ret;
}

int opt_parse(struct options_s *opts, int argc, char **argv)
{
	static const char err_inv[] = "Invalid -%c value.\n";
	double rs = 0, ws = 0;
	size_t blk;
	int opt;

	set_default(opts);

	opterr = 0;
	while ((opt = getopt(argc, argv, "i:o:b:B:m:n:N:H:r1ytlLcCghp:P:u:U:")) != -1) {
		switch (opt) {
			case 'n':
				opts->rcnt = (size_t)get_ul(optarg);
				if (errno || opts->rcnt < 1 || opts->rcnt > DEF_MAXCNT) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'N':
				opts->wcnt = (size_t)get_ul(optarg);
				if (errno || opts->wcnt < 1 || opts->wcnt > DEF_MAXCNT) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'i':
				unset_in(opts);
				if ((opt_subparse(opts, optarg, 0)) < 0) {
					fprintf (stderr, "Bad input file descriptor specification.\n");
					goto out;
				}
				break;
			case 'o':
				unset_out(opts);
				if ((opt_subparse(opts, optarg, 1)) < 0) {
					fprintf (stderr, "Bad output file descriptor specification.\n");
					goto out;
				}
				break;
			case 'b':
				opts->rblk = (size_t)get_ul(optarg);
				if (errno || opts->rblk < 1 || opts->rblk > DEF_MAXBLK) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'B':
				opts->wblk = (size_t)get_ul(optarg);
				if (errno || opts->wblk < 1 || opts->wblk > DEF_MAXBLK) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'm':
				opts->bsiz = (size_t)get_ul(optarg);
				if (errno || opts->bsiz < get_page() || opts->bsiz > SIZE_MAX/2u) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'H':
				opts->hpage = (size_t)get_ul(optarg);
				if (errno || opts->hpage < get_page() || opts->hpage > DEF_MAXHPAGE) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'p':
				rs = get_double(optarg);
				if (errno || rs <= 0.0 || rs >= 1.0) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				rs = 1.0 - rs;
				break;
			case 'P':
				ws = get_double(optarg);
				if (errno || ws <= 0.0 || ws >= 1.0) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
#ifdef h_affi
			case 'u':
				opts->cpuR = (int)get_ul(optarg);
				if (errno || opts->cpuR >= CPU_SETSIZE) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
			case 'U':
				opts->cpuW = (int)get_ul(optarg);
				if (errno || opts->cpuW >= CPU_SETSIZE) {
					fprintf(stderr, err_inv, opt);
					goto out;
				}
				break;
#endif
			case 'g':
				opts->loop = 1;
				break;
#ifdef h_thr
			case 't':
				opts->mode = mt;
				break;
#endif
#ifndef h_mingw
			case '1':
				opts->mode = sp;
				break;
#endif
			case 'y':
				opts->fsync = 1;
				break;
			case 'r':
				opts->strict = 1;
				break;
			case 'l':
				opts->rline = 1;
				break;
			case 'L':
				opts->wline = 1;
				break;
			case 'c':
				opts->rcrc = 1;
				break;
			case 'C':
				opts->wcrc = 1;
				break;
			case 'h':
				help();
				goto out;
			default:
				fprintf(stderr, "\nunknown option: -%c\n", opt);
				goto out;
		}
	}
	if (opts->wline && opts->strict) {
		fputs("Strict mode makes no sense with writer in line mode.\n", stderr);
		goto out;
	}

	blk = Y_MAX(opts->rblk, opts->wblk);
	opts->rsp = (size_t)(0.5 + rs*(double)opts->bsiz);
	opts->wsp = (size_t)(0.5 + ws*(double)opts->bsiz);

	/* we require at least blk + 1 space in both cases (some sharp inequalities are possible in some cases) */
	if (opts->rsp) {
		if (opts->rsp <= blk || opts->rsp >= opts->bsiz - blk) {
			fputs("Read stall point is too extreme.\n", stderr);
			goto out;
		}
	}
	if (opts->wsp) {
		if (opts->wsp <= blk || opts->wsp >= opts->bsiz - blk) {
			fputs("Write stall point is too extreme.\n", stderr);
			goto out;
		}
	}
	if (opts->bsiz <= 2*blk) {
		fputs("Buffer size must be greater than 2*max(rblk, wblk),\n"
		      "  to avoid corner cases.\n", stderr);
		goto out;
	}
#if 0
	if (opts->rblk < opts->wblk && opts->rline && !opts->wline) {
		fputs("\nrblk must be greater or equal to wblk, if the left side is in the \"line\" mode,\n    and the right side is in the reblocking mode.\n", stderr);
		goto out;
	}
#endif

	return 0;
out:
	return -1;
}
