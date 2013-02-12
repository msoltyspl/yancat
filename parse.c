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
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#ifndef h_mingw
# include <sys/socket.h>
# include <netinet/in_systm.h>
# include <netinet/in.h>
# include <netinet/ip.h>
# include <netinet/tcp.h>
# include <arpa/inet.h>
# include <netdb.h>
#else
# include <winsock2.h>
#endif

#include "common.h"
#include "parse.h"

#define SOPT_LEN 40
#define DEF_PORT 10841u
#define badstr (-2)
#define badopt (-3)
#define nomem  (-4)
#define badcnv (-5)
#define baddom (-6)

#ifndef IP_TOS
# define IP_TOS 0
#endif
#ifndef TCP_MAXSEG
# define TCP_MAXSEG 0
#endif
#if 0
#ifndef TCP_QUICKACK
# define TCP_QUICKACK 0
#endif
#ifndef TCP_CORK
# define TCP_CORK 0
#endif
#ifndef UDP_CORK
# define UDP_CORK 0
#endif
#endif

/* apparently for win hosts everything is problematic ... */
#ifndef IPTOS_LOWDELAY
# define IPTOS_LOWDELAY 0
#endif
#ifndef IPTOS_THROUGHPUT
# define IPTOS_THROUGHPUT 0
#endif
#ifndef IPTOS_RELIABILITY
# define IPTOS_RELIABILITY 0
#endif
#ifndef IPTOS_LOWCOST
# define IPTOS_LOWCOST 0
#endif

static struct stropts_s {
	const char *name;
	int lvl, opt;
} opts_by_str[] = {
	{ "IP_TOS",		IPPROTO_IP,	IP_TOS		},
	{ "SO_REUSEADDR",	SOL_SOCKET,	SO_REUSEADDR	},
	{ "SO_SNDBUF",		SOL_SOCKET,	SO_SNDBUF	},
	{ "SO_RCVBUF",		SOL_SOCKET,	SO_RCVBUF	},
	{ "TCP_MAXSEG",		IPPROTO_TCP,	TCP_MAXSEG	},
	{ "TCP_NODELAY",	IPPROTO_TCP,	TCP_NODELAY	},
#if 0
	/* not really useful for us */
	{ "TCP_QUICKACK",	IPPROTO_TCP,	TCP_QUICKACK	},
	{ "TCP_CORK",		IPPROTO_TCP,	TCP_CORK	},
	{ "UDP_CORK",		IPPROTO_UDP,	UDP_CORK	},
#endif
};

static struct strvals_s {
	const char *name;
	int val;
} vals_by_str[] = {
	{ "IPTOS_LOWDELAY",	IPTOS_LOWDELAY		},
	{ "IPTOS_THROUGHPUT",	IPTOS_THROUGHPUT	},
	{ "IPTOS_RELIABILITY",	IPTOS_RELIABILITY	},
	{ "IPTOS_LOWCOST",	IPTOS_LOWCOST		},
};

void sp_list_known(void)
{
	unsigned int i;
	fputs("Known socket options:\n", stderr);
	for (i = 0; i < sizeof(opts_by_str) / sizeof(opts_by_str[0]); i++) {
		if (!opts_by_str[i].opt)
			continue;
		fputs(opts_by_str[i].name, stderr);
		fputc('\n', stderr);
	}
	fputs("\nKnown socket options' values:\n", stderr);
	for (i = 0; i < sizeof(vals_by_str) / sizeof(vals_by_str[0]); i++) {
		if (!vals_by_str[i].val)
			continue;
		fputs(vals_by_str[i].name, stderr);
		fputc('\n', stderr);
	}
}

const char *sp_getsobyint(int opt, int lvl)
{
	unsigned int i;
	for (i = 0; i < sizeof(opts_by_str) / sizeof(opts_by_str[0]); i++) {
		if (!opts_by_str[i].opt)
			continue;
		if (opt == opts_by_str[i].opt && lvl == opts_by_str[i].lvl) {
			return opts_by_str[i].name;
		}
	}
	return NULL;
}

int sp_getsobystr(const char *name, int *lvl)
{
	unsigned int i;
	if (!name || !*name)
		return -1;
	for (i = 0; i < sizeof(opts_by_str) / sizeof(opts_by_str[0]); i++) {
		if (opts_by_str[i].opt && !strcasecmp(name, opts_by_str[i].name)) {
			*lvl = opts_by_str[i].lvl;
			return opts_by_str[i].opt;
		}
	}
	return -1;
}

/*
 * null / empty are assumed 1
 * unknown to host / unconvertable are assumed 0
 *
 * generally value can be named, empty or just a value;
 * empty name is treated as 1 - e.g. socet options that are binary on/off
 * flips, such as reuseaddr; if it's unknown to the table, it's attempted to be
 * converted; successful conversion returns value, unsuccessful returns 0 and set
 * errno; furthermore - unsupported options on some host (mostly windows) are
 * set in table as 0 (which lets them be easily ignored)
 */
int sp_getvalbystr(const char *name)
{
	unsigned int i;
	int val;

	if (!name || !*name)
		return 1;
	for (i = 0; i < sizeof(vals_by_str) / sizeof(vals_by_str[0]); i++) {
		if (vals_by_str[i].val && !strcasecmp(name, vals_by_str[i].name)) {
			return vals_by_str[i].val;
		}
	}
	val = (int)get_ul(name);
	return val;
}

void sp_addsobyint(struct nopts_s *opts, int *copts, int opt, int lvl, int val, int ovr)
{
	int i;
	if (!opt)
		return;
	for (i = 0; i < *copts; i++) {
		if (opts[i].opt == opt && opts[i].lvl == lvl)
			break;
	}
	if (i == SOPT_CNT)
		return;
	/* not found or overridable */
	if (i == *copts || ovr) {
		opts[i].opt = opt;
		opts[i].val = val;
		opts[i].lvl = lvl;
	}
	if (i == *copts)
		(*copts)++;
}

void sp_delsobyidx(struct nopts_s *opts, int *copts, int idx)
{
	if (!copts || !*copts || idx < 0 || idx >= *copts - 1)
		return;
	memmove(opts + idx, opts + idx + 1, sizeof(struct nopts_s)*(size_t)(*copts - 1 - idx));
	(*copts)--;
}

void sp_addsodefs(struct nopts_s *opts, int *copts, int dom)
{
	sp_addsobyint(opts, copts, SO_REUSEADDR, SOL_SOCKET, 1, 0);
	sp_addsobyint(opts, copts, IP_TOS, IPPROTO_IP, IPTOS_THROUGHPUT, 0);

	if (dom == IPPROTO_TCP) {
#if defined(h_mingw) && (_WIN32_WINNT < 0x0600)
		sp_addsobyint(opts, copts, SO_SNDBUF, SOL_SOCKET, 6*65536, 0);
		sp_addsobyint(opts, copts, SO_RCVBUF, SOL_SOCKET, 6*65536, 0);
#endif
	} else {
		sp_addsobyint(opts, copts, SO_SNDBUF, SOL_SOCKET, 18000 - 2*28, 0);
#if defined(h_mingw) && (_WIN32_WINNT < 0x0600)
		sp_addsobyint(opts, copts, SO_RCVBUF, SOL_SOCKET, 65535, 0);
#endif
	}
}

/*
 * TODO move this mess to flex + bison
 */
int sp_addr_parse(struct netpnt_s *a, const char *spec, int dom)
{
	char *idx, *idx2, *idx3, *idx4;
	char ostr[SOPT_LEN];
	ssize_t len, len2;
	int val, opt, lvl, ret = -1;

	if (!spec)
		return -1;
	memset(a, 0, sizeof *a);

	a->port = DEF_PORT;
	switch (dom) {
		case SPEC_IS_TCP:
			a->dom = IPPROTO_TCP;
			break;
		case SPEC_IS_UDP:
			a->dom = IPPROTO_UDP;
			break;
		default:
			ret = baddom;
			goto out;
	}

	idx = strchr(spec,':');
	/* if we have port separator */
	if (idx) {
		len = idx - spec;
		idx++;
		val = (int)get_ul(idx);
		if (errno) {
			ret = badcnv;
			goto out;
		}
		if (val)
			a->port = (unsigned short int)val;
	} else {
		len = strlen(spec);
	}

	if (len >= HOST_LEN) {
		ret = badstr;
		goto out;
	} else if (!len) {
		len++;
		a->host[0] = '*';
	} else
		memcpy(a->host, spec, len);
	a->host[len] = 0;

	if (!idx)
		/* no port separator => ok out */
		goto ok;

	spec = strchr(idx, ':');
#if 0
	if (!spec)
		/* no proto separator => ok out */
		goto ok;
	spec++;
	idx = strchr(spec,':');

	if (idx) {
		len = idx - spec;
	} else {
		len = strlen(spec);
	}

	len2 = len <= 3 ? 3 : len;
	if (!strncasecmp("udp", spec, len2))
		a->dom = IPPROTO_UDP;
	else if (len && strncasecmp("tcp", spec, len2)) {
		ret = baddom;
		goto out;
	}
	spec = idx;
#endif
	if (!spec || !spec[1])
		/* no opts separator or empty => bail out */
		goto ok;
	do {
		spec++;
		idx = strchr(spec, ',');
		idx2 = strchr(spec, '=');
		if (idx2 && (!idx || idx2 < idx)) {
			len = idx2 - spec;
			idx3 = idx2;
			val = 0;
			do {
				idx3++;
				idx4 = strchr(idx3, '|');
				if (idx4 && (!idx || idx4 < idx))
					len2 = idx4 - idx3;
				else if (idx)
					len2 = idx - idx3;
				else
					len2 = strlen(idx3);
				if (len2 >= SOPT_LEN) {
					len = len2; spec = idx3;
					ret = badstr;
					goto out;
				}
				memcpy(ostr, idx3, len2);
				ostr[len2] = 0;
				val |= sp_getvalbystr(ostr);
			} while ((idx3 = idx4) && (!idx || idx3 < idx));
		} else {
			if (idx)
				len = idx - spec;
			else
				len = strlen(spec);
			val = 1;
		}
		if (len >= SOPT_LEN) {
			ret = badstr;
			goto out;
		}
		memcpy(ostr, spec, len);
		ostr[len] = 0;
		opt = sp_getsobystr(ostr, &lvl);
		if (opt < 0) {
			spec = ostr;
			ret = badopt;
			goto out;
		}
		sp_addsobyint(a->opts, &a->copts, opt, lvl, val, 1);
	} while (a->copts < SOPT_CNT && (spec = idx));
ok:
	sp_addsodefs(a->opts, &a->copts, a->dom);
	return 0;
out:
	fputs("sp_addr_parse(): ", stderr);
	switch (ret) {
		case badstr:
			fprintf(stderr,"string too long: %.*s\n", (int)len, spec);
			break;
		case badopt:
			fprintf(stderr,"unknown option: %.*s\n", (int)len, spec);
			break;
		case nomem:
			fputs("not enough memory ....\n", stderr);
			break;
		case baddom:
			fputs("unknown protocol\n", stderr);
			break;
		case badcnv:
			fputs("can't convert\n", stderr);
			break;
		default:
			fputs("unspecified error\n", stderr);
	}
	return ret;
}
