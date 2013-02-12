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
/*
 * Note: no C++ stuff please ...
 */
#include "config.h"
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#ifdef h_thr
# include <pthread.h>
#endif
#ifdef h_affi
# include <sched.h>
#endif
#ifdef h_mingw
# include <winsock2.h>
#else
# include <sys/types.h>
# include <sys/wait.h>
#endif

#include "common.h"
#include "fdpack.h"
#include "options.h"
#include "parse.h"
#include "mtxw.h"
#include "semw.h"
#include "shmw.h"
#include "buffer.h"

static struct options_s g_opts;
static struct fdpack_s g_fdi, g_fdo;
static pid_t g_pid = -1;

#define errl_mt 0
#define errl_ms 1
#define errl_mi 2
#define errl_st 3
#define errl_ss 4
#define errl_si 5
#define errl_cnt (errl_si + 1)

static const char *errlog_str[errl_cnt] = {
	[errl_mt] = "merr!",
	[errl_ms] = "msig!",
	[errl_mi] = "mini!",
	[errl_st] = "serr!",
	[errl_ss] = "ssig!",
	[errl_si] = "sini!",
};

static struct shr_s {
	struct buf_s buf;
	size_t xrsiz, xwsiz;
	sig_atomic_t done, mwait, swait;
	sig_atomic_t errlog[errl_cnt];
#ifndef h_mingw
	struct mtx_s vars;
	struct sem_s nospace, nodata, sync2s;
#endif
} *g_shm = NULL;

static struct buf_s *g_buf;
static struct shm_s g_chunk;

#ifndef h_mingw
static struct sem_s *g_nospace, *g_nodata, *g_sync2s;
static struct mtx_s *g_vars;

#if 0
static int sigs_ign[] = { SIGPIPE, SIGTTIN, SIGTTOU, SIGHUP, SIGUSR1, SIGUSR2 };
static int sigs_end[] = { SIGTERM, SIGINT, SIGCHLD };
#endif

static int sigs_ign[] = { SIGPIPE, SIGTTIN, SIGTTOU, SIGHUP, SIGUSR1, SIGUSR2, SIGCHLD };
static int sigs_end[] = { SIGTERM, SIGINT };

#endif

static void release_master(void)
{
	g_shm->errlog[errl_si] = 1;
	g_shm->done = 2;
	__cmb();
#ifndef h_mingw
	Vb(g_nospace);
	Vb(g_sync2s);
#endif
}

static void release_slave(void)
{
	g_shm->errlog[errl_mi] = 1;
	g_shm->done = 2;
	__cmb();
#ifndef h_mingw
	Vb(g_nodata);
#endif
}

static int cleanup_master(void)
{
	buf_dtor(g_buf);
#ifndef h_mingw
	semw_dtor(g_nodata);
	semw_dtor(g_nospace);
	semw_dtor(g_sync2s);
	mtxw_dtor(g_vars);
#endif
	shmw_dtor(&g_chunk);
	return 0;
}

static int cleanup_slave(void)
{
	buf_dt(g_buf);
#ifndef h_mingw
	semw_dt(g_nodata);
	semw_dt(g_nospace);
	semw_dt(g_sync2s);
	mtxw_dt(g_vars);
#endif
	shmw_dt(&g_chunk);
	return 0;
}

#ifndef h_mingw

static void sh_rreq_end(int sig __attribute__ ((__unused__)))
{
	g_shm->done = 2;
	g_shm->errlog[errl_ms] = 1;
	__cmb();
}

static void sh_wreq_end(int sig __attribute__ ((__unused__)))
{
	g_shm->done = 2;
	g_shm->errlog[errl_ss] = 1;
	__cmb();
}

static int setup_sig(int s, void (*f)(int))
{
	struct sigaction igp;

	igp.sa_handler = f;
	/* igp.sa_flags = SA_RESTART; */
	igp.sa_flags = SA_NOCLDSTOP;
	sigfillset(&igp.sa_mask);

	if (sigaction(s, &igp, NULL) < 0) {
		fprintf(stderr, "sigaction(): can't set handler of '%s': %s\n", strsignal(s), strerror(errno));
		return -1;
	}
	return 0;
}

static int setup_sigs(const int *tab, int cnt, void (*f)(int))
{
	int i;
	for (i = 0; i < cnt; i++) {
		if (setup_sig(tab[i], f) < 0)
			return -1;
	}
	return 0;
}

#endif

static int setup_env(void)
{
	int noshr;
	int mp = !(g_opts.thread || g_opts.onetask);
	size_t siz = sizeof(struct shr_s);

	fputc('\n', stderr);

	if ((noshr = shmw_ctor(&g_chunk, "/yancat-main", &siz, 0, 0, 1)) < 0)
		return -1;
	mp = mp && !noshr;
	g_shm = (struct shr_s *)shmw_ptr(&g_chunk);
	memset(g_shm, 0, siz);
#ifndef h_mingw
	if (mtxw_ctor(&g_shm->vars, "/yancat-vars", mp) < 0)
		goto out1;
	g_vars = &g_shm->vars;
	if (semw_ctor(&g_shm->nospace, "/yancat-nospace", mp, 0) < 0)
		goto out2;
	g_nospace = &g_shm->nospace;
	if (semw_ctor(&g_shm->nodata, "/yancat-nodata", mp, 0) < 0)
		goto out3;
	g_nodata = &g_shm->nodata;
	if (semw_ctor(&g_shm->sync2s, "/yancat-sync2s", mp, 0) < 0)
		goto out4;
	g_sync2s = &g_shm->sync2s;
#endif

	/* so far so good, now buffer object */
	if ((noshr = buf_ctor(&g_shm->buf, g_opts.bsiz, g_opts.rblk, g_opts.wblk, g_opts.hpage)) < 0) {
		fprintf (stderr, "setup_env(): buffer initialization failed.\n");
		goto out5;
	}
	mp = mp && !noshr;

	g_buf = &g_shm->buf;
	buf_setextra(g_buf, g_opts.rline, g_opts.wline, g_opts.rcrc, g_opts.wcrc, g_opts.rsp, g_opts.wsp);

	/* return 1 if we should fork */
	return mp;
out5:
#ifndef h_mingw
	semw_dtor(g_sync2s);
out4:
	semw_dtor(g_nodata);
out3:
	semw_dtor(g_nospace);
out2:
	mtxw_dtor(g_vars);
out1:
#endif
	shmw_dtor(&g_chunk);
	return -1;
}

static int setup_proc(int mp __attribute__ ((__unused__)))
{
#ifndef h_mingw
	void (*f)(int);

	/* this is common for both processes */
	if (setup_sigs(sigs_ign, sizeof(sigs_ign)/sizeof(int), SIG_IGN) < 0)
		goto out;
	/*
	 * if buffer returned 1, it means the chunk it allocated is not shared,
	 * so we can't go multiprocessor mode
	 */
	if (mp) {
		fputs("Continuing with 2 processes.\n", stderr);
		g_pid = fork();
		if (g_pid == -1) {
			perror("fork()");
			goto out;
		}
		if (g_pid) {
			/* reader/master */
			f = &sh_rreq_end;
		} else {
			/* writer/slave */
			f = &sh_wreq_end;
		}
	} else {
		fputs("Continuing with 1 process.\n", stderr);
		f = &sh_rreq_end;
	}

	if (setup_sigs(sigs_end, sizeof(sigs_end)/sizeof(int), f) < 0)
		goto out;
	return 0;
out:
	if (g_pid) {
		release_slave();
	} else {
		release_master();
	}
	return -1;
#else
	return 0;
#endif
}

static int setup_winsuck(void)
{
#ifdef h_mingw
	WSADATA wsaData;
	int ws2;

	ws2 = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ws2 || LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		fputs("Couldn't initialize winsock dll.\n", stderr);
		return -1;
	}
#endif
	return 0;
}

static int cleanup_winsuck(void)
{
#ifdef h_mingw
	WSACleanup();
#endif
	return 0;
}

static int setup_fds(void)
{
	if (fd_ctor(&g_fdi, 0, g_opts.fd[0], g_opts.fsync) < 0)
	if (fd_ctor_f(&g_fdi, 0, g_opts.file[0], g_opts.fsync) < 0)
	if (fd_ctor_s(&g_fdi, 0, &g_opts.sock[0], !g_opts.rline) < 0)
	if (fd_ctor(&g_fdi, 0, "0", 0) < 0)
		return -1;

	if (fd_ctor(&g_fdo, 1, g_opts.fd[1], g_opts.fsync) < 0)
	if (fd_ctor_f(&g_fdo, 1, g_opts.file[1], g_opts.fsync) < 0)
	if (fd_ctor_s(&g_fdo, 1, &g_opts.sock[1], 0) < 0)
	if (fd_ctor(&g_fdo, 1, "1", g_opts.fsync) < 0)
		goto out;

	fprintf (stderr, "\nPre-open input side:\n");
	fd_info(&g_fdi);
	fprintf (stderr, "\nPre-open output side:\n");
	fd_info(&g_fdo);
	if (g_opts.strict)
		fputs("\nStrict reblocking writes enabled.\n", stderr);
	fflush(stderr);

	return 0;
out:
	fd_dtor(&g_fdi);
	return -1;
}

static void cleanup_fds(void)
{
	fd_dtor(&g_fdi);
	fd_dtor(&g_fdo);
}

static void cleanup_env(void)
{
	if (g_pid) {
		cleanup_master();
	} else {
		cleanup_slave();
	}
}

static inline ssize_t
read_i(struct fdpack_s *fd, uint8_t *restrict buf, size_t blk)
{
	ssize_t ret;
	do {
		ret = fd_read(fd, buf, blk);
		__cvmb(g_shm->done);
	} while (unlikely(ret < 0) && errno == EINTR && !g_shm->done);
	return ret;
}

static inline ssize_t
write_i(struct fdpack_s *fd, const uint8_t *restrict buf, size_t blk)
{
	ssize_t ret;
	do {
		ret = fd_write(fd, buf, blk);
		__cvmb(g_shm->done);
	} while (unlikely(ret < 0) && errno == EINTR && !g_shm->done);
	return ret;
}

static int transfer_summary(void)
{
	int i, ret = 0;

	if (!g_pid)
		return 0;

	fputs("\nTransfer summary:\n ", stderr);
	for (i = 0; i < errl_cnt ; i++) {
		if (g_shm->errlog[i]) {
			fputc(' ', stderr);
			fputs(errlog_str[i], stderr);
			ret = -1;
		}
	}
	if (!ret)
		fputs(" completed cleanly\n", stderr);
	else
		fputc('\n', stderr);
#if 0
	if (g_shm->done == 1) {
		fputs("  completed cleanly\n", stderr);
		ret = 0;
	} else {
		fputs("  init/transfer issues and/or signalled\n", stderr);
		ret = -1;
	}
#endif
	return ret;
}

/* reader process */
static void transfer_master(void)
{
#ifndef h_mingw
	uint8_t *ptrr;
	int errR = 0;
	ssize_t retr = 1;
	size_t siz;

	Pm(g_vars);
	while likely(!g_shm->done) {
		siz = buf_can_r(g_buf);
		if unlikely(!siz) {
			g_shm->mwait = 1;
			Vm(g_vars);
			Pb(g_nospace);
			/*
			 * passing this 'if' guarantees xrsiz != 0;
			 * analogously for slave; if we're out, we save on
			 * one semaphore call, which is always nice =)
			 */
			if unlikely(g_shm->done)
				goto outt;
			siz = g_shm->xrsiz;
			//Pb(g_vars);
			//continue;
		} else
			Vm(g_vars);
		ptrr = buf_fetch_r(g_buf, siz);
		retr = read_i(&g_fdi, ptrr, siz);
		if unlikely(retr <= 0) {
			if (retr < 0)
				errR = errno;
			goto outt;
		}
		buf_commit_r(g_buf, retr);
		Pm(g_vars);
		buf_commit_rf(g_buf, retr);
		/* wake up writer, if it's suspended due to data */
		if (unlikely(g_shm->swait) && (siz = buf_can_w(g_buf))) {
			g_shm->swait = 0;
			g_shm->xwsiz = siz;
			Vb(g_nodata);
		}
	}
	Vm(g_vars);
outt:
	/*
	 * only we can set 'done' to 1 - any other events (init, signal, error)
	 * will always set it to 2;
	 *
	 * this gives us clean indication that master exited from its reading
	 * loop and slave can execute its epilogue safely; also note cmpxchg -
	 * we never change already changed flag
	 *
	 * also see relevant comments in slave
	 */
	if (retr < 0) {
		g_shm->errlog[errl_mt] = 1;
		g_shm->done = 2;
	} else if (retr == 0)
		__cmpxchg(g_shm->done, 0, 1);
	Vb(g_nodata);
	Pb(g_sync2s);
	if (retr < 0)
		fprintf(stderr, "\nread()/recv(): %s\n", strerror(errR));
#endif
}

static ssize_t transfer_slave_epi(int *errW)
{
	uint8_t *ptrw;
	ssize_t retw = 1;
	size_t siz, pad;

	buf_setlinew(g_buf);
	while likely(siz = buf_can_w(g_buf)) {
		if (unlikely(siz < g_opts.wblk) && g_opts.strict) {
			ptrw = buf_fetch_w(g_buf, g_opts.wblk);
			pad = g_opts.wblk - siz;
			fprintf (stderr, "\nWriter: padding with %zu 0s\n", pad);
			/*
			 * this is safe - buffer always has more than
			 * 2*max(wblk,rblk), and we're not reading anymore
			 */
			memset(ptrw + siz, 0, pad);
		} else {
			ptrw = buf_fetch_w(g_buf, siz);
			pad = 0;
		}
		retw = write_i(&g_fdo, ptrw, siz + pad);
		if unlikely(retw < 0) {
			*errW = errno;
			break;
		}
		if unlikely((size_t)retw > siz) {
			buf_commit_w(g_buf, retw);
			//buf_commit_w(g_buf, siz);
			//buf_commit_pad(g_buf, ptrw + siz, retw - siz);
			buf_commit_wf(g_buf, siz);
			break;
		}
		buf_commit_w(g_buf, retw);
		buf_commit_wf(g_buf, retw);
	}
	return retw;
}

static void transfer_slave(void)
{
#ifndef h_mingw
	uint8_t *ptrw;
	int errW = 0;
	ssize_t retw = 1;
	size_t siz;

	Pm(g_vars);
	while likely(!g_shm->done) {
		siz = buf_can_w(g_buf);
		if unlikely(!siz) {
			g_shm->swait = 1;
			Vm(g_vars);
			Pb(g_nodata);
			if unlikely(g_shm->done)
				goto outt;
			siz = g_shm->xwsiz;
			//Pm(g_vars);
			//continue;
		} else
			Vm(g_vars);
		ptrw = buf_fetch_w(g_buf, siz);
		retw = write_i(&g_fdo, ptrw, siz);
		if unlikely(retw < 0) {
			errW = errno;
			goto outt;
		}
		if (unlikely((size_t)retw < g_opts.wblk) && g_opts.strict)
			fprintf (stderr, "warning, strict mode writer wrote %zd instead of %zu\n", retw, g_opts.wblk);
		buf_commit_w(g_buf, retw);
		Pm(g_vars);
		buf_commit_wf(g_buf, retw);
		/* wake up reader, if it's suspended due to nospace */
		if (unlikely(g_shm->mwait) && (siz = buf_can_r(g_buf))) {
			g_shm->mwait = 0;
			g_shm->xrsiz = siz;
			Vb(g_nospace);
		}
	}
	Vm(g_vars);
outt:
	/*
	 * if we're here, we get either signalled by master, or errored - so
	 * set 'done' only in the latter case
	 */
	if unlikely(retw < 0) {
		g_shm->errlog[errl_st] = 1;
		g_shm->done = 2;
	}
	Vb(g_nospace);

	/*
	 * epilogue may be run only if master is outside its reading loop;
	 * it's important because we don't use any locking in epilogue, and
	 * we also pad 0s in strict reblocking mode /into/ fetched buffer;
	 *
	 * note: master cannot override it to 1 due to atomic cmpxchg; without
	 * it we could doublecheck with retw >= 0 to avoid unnecessary call,
	 * but we could still lose external events (functionally not a problem
	 * though)
	 */
	if (g_shm->done == 1)
		retw = transfer_slave_epi(&errW);
/* oute: */
	if (retw < 0) {
		g_shm->errlog[errl_st] = 1;
		fprintf (stderr, "\nwrite()/send(): %s\n", strerror(errW));
	}
	/* signal reader we're done with writing */
	Vb(g_sync2s);
#endif
}

static void transfer_1cpu(void)
{
	uint8_t *ptrr, *ptrw;
	int errR = 0, errW = 0;
	ssize_t retr = 1, retw = 0;
	size_t siz, cnt;

	while likely(!g_shm->done) {
		cnt = g_opts.rcnt;
		while likely((siz = buf_can_r(g_buf)) && cnt--) {
			ptrr = buf_fetch_r(g_buf, siz);
			retr = read_i(&g_fdi, ptrr, siz);
			if unlikely(retr <= 0) {
				if (retr < 0)
					errR = errno;
				goto outt;
			}
			buf_commit_r(g_buf, retr);
			buf_commit_rf(g_buf, retr);
		}
		cnt = g_opts.wcnt;
		while likely((siz = buf_can_w(g_buf)) && cnt--) {
			ptrw = buf_fetch_w(g_buf, siz);
			retw = write_i(&g_fdo, ptrw, siz);
			if unlikely(retw < 0) {
				errW = errno;
				goto outt;
			}
			buf_commit_w(g_buf, retw);
			buf_commit_wf(g_buf, retw);
			if (unlikely((size_t)retw < g_opts.wblk) && g_opts.strict)
				fprintf(stderr, "warning, strict mode writer wrote %zd instead of %zu\n", retw, g_opts.wblk);
		}
		__cvmb(g_shm->done);
	}
outt:
	if (retw >= 0)
		retw = transfer_slave_epi(&errW);
/* oute: */

	if (retr < 0 || retw < 0) {
		g_shm->errlog[errl_mt] = 1;
		fputc('\n', stderr);
		if (retr < 0)
			fprintf(stderr, "read()/recv(): %s\n", strerror(errR));
		if (retw < 0)
			fprintf(stderr, "write()/send(): %s\n", strerror(errW));
	}
}

void setaffi(int cpu __attribute__ ((__unused__)),
	     const char *tag __attribute__ ((__unused__)))
{
#ifdef h_affi
	static const char *warn = "affinity: %s: %s\n";
	cpu_set_t cpus;
	int ret;

	if (cpu < 0)
		return;

	CPU_ZERO(&cpus);
	CPU_SET((size_t)cpu, &cpus);
# ifdef h_thr
	ret = pthread_setaffinity_np(pthread_self(), sizeof cpus, &cpus);
# else
	ret = 0;
	if (sched_setaffinity(getpid(), sizeof cpus, &cpus) < 0)
		ret = errno;
# endif
	if (ret != 0)
		fprintf(stderr, warn, tag, strerror(ret));
#endif
}

static void *main_slave(void *arg)
{
	const char *tag = arg;
	if (fd_open(&g_fdo) < 0) {
		release_master();
		return NULL;
	}

	setaffi(g_opts.cpuS, tag);
	transfer_slave();
	return NULL;
}

void main_master(int forked)
{
	buf_report_init(g_buf);
	fputc('\n', stderr);

	if (fd_open(&g_fdi) < 0) {
		release_slave();
		return;
	}

	if (!forked) {
#ifdef h_thr
		if (g_opts.thread) {
			int ret;
			pthread_t thr;
			fputs("Continuing with 2 threads.\n", stderr);
			if ((ret = pthread_create(&thr, NULL, main_slave, "slave thread"))) {
				fprintf(stderr, "pthread_create(): %s\n", strerror(ret));
				return;
			}
			setaffi(g_opts.cpuM, "master thread");
			transfer_master();
			pthread_join(thr, NULL);
		}
		else
#endif
		{
			if (fd_open(&g_fdo) < 0)
				return;
			transfer_1cpu();
		}
	} else {
		setaffi(g_opts.cpuM, "master process");
		transfer_master();
	}

	buf_report_stats(g_buf);
}

int main(int argc, char **argv)
{
	int mp, ret = -1;

	if (common_init() < 0)
		goto out1;
	if (opt_parse(&g_opts, argc, argv) < 0)
		goto out1;
	if (setup_winsuck() < 0)
		goto out1;
	do {
		if ((ret = setup_fds()) < 0)
			goto out2;
		if ((mp = ret = setup_env()) < 0)
			goto out3;
		if ((ret = setup_proc(mp)) < 0)
			goto out4;
		/* one or two processes from here on */
		if (g_pid) {
			main_master(mp);
		} else
			main_slave("slave process");
#ifndef h_mingw
		waitpid(0, NULL, 0);
#endif
		ret = transfer_summary();
out4:
		cleanup_env();
out3:
		cleanup_fds();
	} while (g_opts.loop && ret >= 0 && g_pid);
out2:
	cleanup_winsuck();
out1:
	fflush(stderr);
	return ret;
}
