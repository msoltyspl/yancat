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

enum role_t {arbiter = 0, reader, writer, crcer};

static struct options_s g_opts;
static struct fdpack_s g_fdi, g_fdo;

/* crc not implemented yet as a separate thread */
#ifdef h_thr
static pthread_t g_threads[4] = {0};
static __thread enum role_t g_role = arbiter;
#else
static enum role_t g_role = arbiter;
#endif

/* these must follow role_t enum definition */
#define ERRL_CNT (sizeof(errlog_str)/sizeof(errlog_str[0]))
static const char *errlog_str[] = {
	[0] = "arb_sig!", [1] = "rdr_sig!", [2]  = "wrr_sig!", [3]  = "crc_sig!", 
	[4] = "arb_ini!", [5] = "rdr_ini!", [6]  = "wrr_ini!", [7]  = "crc_ini!", 
	[8] = "arb_err!", [9] = "rdr_err!", [10] = "wrr_err!", [11] = "crc_err!", 
};

static struct shm_s g_chunk;

static struct shr_s {
	struct buf_s buf;
	size_t xrsiz, xwsiz;
	int errR, errW;
	sig_atomic_t done, mwait, swait;
	sig_atomic_t errlog[ERRL_CNT];
#ifndef h_mingw
	struct mtx_s vars;
	struct sem_s nospace, nodata;
#endif
} *g_shm = NULL;

static struct buf_s *g_buf;

#ifndef h_mingw
static struct mtx_s *g_vars;
static struct sem_s *g_nospace, *g_nodata;

static int sigs_ign[] = { SIGPIPE, SIGTTIN, SIGTTOU, SIGHUP, SIGUSR1, SIGUSR2, SIGCHLD, 0 };
static int sigs_end[] = { SIGTERM, SIGINT, 0 };

#endif

/*
 * called if there was an error during initialization - release all locks, make
 * sure nothing blocks at any point; g_role is thread local, so errlog will get
 * proper value
 */
static void release(void)
{
	g_shm->errlog[4 + g_role] = 1;
	g_shm->done = 2;
	barrier();
#ifndef h_mingw
	if (g_opts.mode != sp) {
		Vb(g_nodata);
		Vb(g_nospace);
	}
#endif
}

static int cleanup_arbiter(void)
{
#ifndef h_mingw
	if (g_opts.mode != sp) {
		semw_dtor(g_nodata);
		semw_dtor(g_nospace);
		mtxw_dtor(g_vars);
	}
#endif
	buf_dtor(g_buf);
	shmw_dtor(&g_chunk);
	return 0;
}

static int cleanup_child(void)
{
#ifndef h_mingw
	if (g_opts.mode != sp) {
		semw_dt(g_nodata);
		semw_dt(g_nospace);
		mtxw_dt(g_vars);
	}
#endif
	buf_dt(g_buf);
	shmw_dt(&g_chunk);
	return 0;
}

#ifndef h_mingw

static void sh_terminate(int sig __attribute__ ((__unused__)))
{
	g_shm->done = 2;
	g_shm->errlog[g_role] = 1;
	barrier();
}

static int setup_sigs(const int *tab, void (*f)(int))
{
	int i;
	struct sigaction igp;

	for (i = 0; tab[i]; i++) {
		igp.sa_handler = f;
		/* note the code works fine whether restart is specified or not */
		igp.sa_flags = SA_NOCLDSTOP | SA_RESTART;
		sigfillset(&igp.sa_mask);
		if (sigaction(tab[i], &igp, NULL) < 0) {
			fprintf(stderr, "sigaction(): can't set handler of '%s': %s\n", strsignal(tab[i]), strerror(errno));
			return -1;
		}
	}
	return 0;
}

#endif

static int setup_env(void)
{
	int noshr;
	int mpok = (g_opts.mode == mp);
	size_t siz = sizeof(struct shr_s);

	fputc('\n', stderr);

#ifndef h_mingw
	/* setup signal handlers */
	if (setup_sigs(sigs_ign, SIG_IGN) < 0)
		return -1;
	if (setup_sigs(sigs_end, &sh_terminate) < 0)
		return -1;
#endif

	/* main shared chunk of memory with buffer, semaphores and so on */
	if ((noshr = shmw_ctor(&g_chunk, "/yancat-main", &siz, 0, 0, 1)) < 0)
		return -1;
	mpok = mpok && !noshr;
	g_shm = (struct shr_s *)shmw_ptr(&g_chunk);
	memset(g_shm, 0, siz);

	/*
	 * buffer object located in the above chunk; buffer itself allocates
	 * main and bounce areas (if applicable)
	 */
	if ((noshr = buf_ctor(&g_shm->buf, g_opts.bsiz, g_opts.rblk, g_opts.wblk, g_opts.hpage)) < 0) {
		fprintf (stderr, "setup_env(): buffer initialization failed.\n");
		goto out1;
	}
	mpok = mpok && !noshr;
	g_buf = &g_shm->buf;

	/* update g_opts.mode to reflect the above) */
	g_opts.mode = mpok ? mp : (g_opts.mode == mt ? mt : sp);

#ifndef h_mingw
	if (g_opts.mode != sp) {
		if (mtxw_ctor(&g_shm->vars, "/yancat-vars", g_opts.mode == mp) < 0)
			goto out2;
		g_vars = &g_shm->vars;
		if (semw_ctor(&g_shm->nospace, "/yancat-nospace", g_opts.mode == mp, 0) < 0)
			goto out3;
		g_nospace = &g_shm->nospace;
		if (semw_ctor(&g_shm->nodata, "/yancat-nodata", g_opts.mode == mp, 0) < 0)
			goto out4;
		g_nodata = &g_shm->nodata;
	}
#endif

	buf_setextra(g_buf, g_opts.rline, g_opts.wline, g_opts.rcrc, g_opts.wcrc, g_opts.rsp, g_opts.wsp);
	buf_report_init(g_buf);

	return 0;
#ifndef h_mingw
out4:
	semw_dtor(g_nospace);
out3:
	mtxw_dtor(g_vars);
out2:
	buf_dtor(g_buf);
#endif
out1:
	shmw_dtor(&g_chunk);
	return -1;
}

#ifndef h_mingw
/*
 * fork / thread, setup signals, etc.
 */
static int forkself(enum role_t role)
{
	pid_t pid;
	pid = fork();
	if (pid == -1) {
		perror("fork()");
		return -1;
	}
	if (!pid)
		g_role = role;
	return pid;
}
#endif

#ifdef h_affi
static void setup_proc_affinity(pid_t pid, int cpu, const char *tag)
{
	cpu_set_t cpus;

	if (cpu < 0)
		return;
	CPU_ZERO(&cpus);
	CPU_SET((size_t)cpu, &cpus);
	if (sched_setaffinity(pid, sizeof cpus, &cpus) < 0)
		fprintf(stderr, "WARN: cannot set the affinity of the %s process: %s\n", tag, strerror(errno));
}

static void setup_thread_affinity(pthread_t thr, int cpu, const char *tag)
{
	int err;
	cpu_set_t cpus;

	if (cpu < 0)
		return;
	CPU_ZERO(&cpus);
	CPU_SET((size_t)cpu, &cpus);
	err = pthread_setaffinity_np(thr, sizeof cpus, &cpus);
	if (err)
		fprintf(stderr, "WARN: cannot set the affinity of the %s thread: %s\n", tag, strerror(err));
}
#endif

static void *task_reader(void *arg __attribute__ ((__unused__)));
static void *task_writer(void *arg __attribute__ ((__unused__)));
// static int setup_proc(int mp __attribute__ ((__unused__)))
static int setup_proc(void)
{
	int ret = 0;

	if (g_opts.mode == sp) {
		fputs("Continuing with single process.\n", stderr);
#ifndef h_mingw
	} else if (g_opts.mode == mp) {
		pid_t rpid, wpid;
		fputs("Continuing with 2 processes.\n", stderr);
		rpid = forkself(reader);
		if (!rpid)
			/* we're reader child, nothing to see here */
			return 0;
		wpid = forkself(writer);
		if (!wpid)
			/* we're writer child, nothing to see here */
			return 0;

		if (rpid < 0 || wpid < 0)
			goto out;
		/* both children forked successfully */

#ifdef h_affi
		/* affinity if applicable */
		setup_proc_affinity(rpid, g_opts.cpuR, "reader");
		setup_proc_affinity(wpid, g_opts.cpuW, "writer");
#endif
#ifdef h_thr
	} else if (g_opts.mode == mt) {
		fputs("Continuing with 2 threads.\n", stderr);
		if ((ret = pthread_create(g_threads + reader, NULL, task_reader, "reader thread")))
			goto out;
		if ((ret = pthread_create(g_threads + writer, NULL, task_writer, "writer thread")))
			goto out;

#ifdef h_affi
		/* affinity if applicable */
		setup_thread_affinity(g_threads[reader], g_opts.cpuR, "reader");
		setup_thread_affinity(g_threads[writer], g_opts.cpuW, "writer");
#endif
#endif
#endif
	}

	return 0;

out:
	/* arbiter only */
	release();
	return -1;
}

static int setup_winsock(void)
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

static int cleanup_winsock(void)
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
	if (g_role == arbiter) {
		cleanup_arbiter();
	} else {
		cleanup_child();
	}
}

static inline ssize_t
read_i(struct fdpack_s *fd, uint8_t *restrict buf, size_t blk)
{
	ssize_t ret;
	do {
		ret = fd_read(fd, buf, blk);
	} while (unlikely(ret < 0) && errno == EINTR && !ACCESS_ONCE(g_shm->done));
	return ret;
}

static inline ssize_t
write_i(struct fdpack_s *fd, const uint8_t *restrict buf, size_t blk)
{
	ssize_t ret;
	do {
		ret = fd_write(fd, buf, blk);
	} while (unlikely(ret < 0) && errno == EINTR && !ACCESS_ONCE(g_shm->done));
	return ret;
}

/* reader process */
static void transfer_reader(void)
{
#ifndef h_mingw
	uint8_t *ptrr;
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
				g_shm->errR = errno;
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
		g_shm->errlog[8 + g_role] = 1;
		g_shm->done = 2;
	} else if (retr == 0)
		cmpxchg(g_shm->done, 0, 1);
	Vb(g_nodata);
#endif
}

static ssize_t transfer_writer_epi(void)
{
	uint8_t *ptrw;
	ssize_t retw = 1;
	size_t siz, pad;

	buf_setlinew(g_buf);
	while likely(siz = buf_can_w(g_buf)) {
		if (unlikely(siz < g_opts.wblk) && g_opts.strict) {
			ptrw = buf_fetch_w(g_buf, g_opts.wblk);
			pad = g_opts.wblk - siz;
			fprintf (stderr, "\nINFO: strict mode writer padded with %zu 0s\n", pad);
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
			g_shm->errW = errno;
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

static void transfer_writer(void)
{
#ifndef h_mingw
	uint8_t *ptrw;
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
			g_shm->errW = errno;
			goto outt;
		}
		if (unlikely((size_t)retw < g_opts.wblk) && g_opts.strict)
			fprintf(stderr, "WARN: strict mode writer wrote %zd instead of %zu\n", retw, g_opts.wblk);
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
		g_shm->errlog[8 + g_role] = 1;
		g_shm->done = 2;
	}
	Vb(g_nospace);

	/*
	 * epilogue may be run only if reader is outside its reading loop;
	 * it's important because we don't use any locking in epilogue, and
	 * we also pad 0s in strict reblocking mode /into/ fetched buffer;
	 *
	 * note: master cannot override it to 1 due to atomic cmpxchg; without
	 * it we could doublecheck with retw >= 0 to avoid unnecessary call,
	 * but we could still lose external events (functionally not a problem
	 * though)
	 */
	if (g_shm->done == 1)
		retw = transfer_writer_epi();
/* oute: */
	if (retw < 0) {
		g_shm->errlog[8 + g_role] = 1;
	}
#endif
}

static void transfer_1cpu(void)
{
	uint8_t *ptrr, *ptrw;
	ssize_t retr = 1, retw = 0;
	size_t siz, cnt;

	while likely(!ACCESS_ONCE(g_shm->done)) {
		cnt = g_opts.rcnt;
		while likely((siz = buf_can_r(g_buf)) && cnt--) {
			ptrr = buf_fetch_r(g_buf, siz);
			retr = read_i(&g_fdi, ptrr, siz);
			if unlikely(retr <= 0) {
				if (retr < 0)
					g_shm->errR = errno;
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
				g_shm->errW = errno;
				goto outt;
			}
			buf_commit_w(g_buf, retw);
			buf_commit_wf(g_buf, retw);
			if (unlikely((size_t)retw < g_opts.wblk) && g_opts.strict)
				fprintf(stderr, "WARN: strict mode writer wrote %zd instead of %zu\n", retw, g_opts.wblk);
		}
	}
outt:
	if (retw >= 0)
		retw = transfer_writer_epi();
/* oute: */

	if (retr < 0 || retw < 0) {
		g_shm->errlog[8 + g_role] = 1;
	}
}

/* note: fd cleanup is performed outside */
static void *task_writer(void *arg __attribute__ ((__unused__)))
{
	g_role = writer;
	if (fd_open(&g_fdo) < 0) {
		release();
		return NULL;
	}
	transfer_writer();
	return NULL;
}

static void *task_reader(void *arg __attribute__ ((__unused__)))
{
	g_role = reader;
	if (fd_open(&g_fdi) < 0) {
		release();
		return NULL;
	}
	transfer_reader();
	return NULL;
}

static void *task_single(void *arg __attribute__ ((__unused__)))
{
	/* role remains arbiter */
	if (fd_open(&g_fdi) < 0)
		goto out;
	if (fd_open(&g_fdo) < 0)
		goto out;

	transfer_1cpu();
	return NULL;
out:
	release();
	return NULL;
}

static int reaper(void)
{
	int i, ret = 0;

#ifndef h_mingw
	if (g_opts.mode == mp) {
		waitpid(0, NULL, 0);
#ifdef h_thr
	} else if (g_opts.mode == mt) {
		for (i = 0; i < sizeof g_threads / sizeof g_threads[0]; i++)
			if (g_threads[i])
				pthread_join(g_threads[i], NULL);
#endif
	}
#endif
	fputs("\nTransfer events:\n ", stderr);
	for (i = 0; i < ERRL_CNT; i++) {
		if (g_shm->errlog[i]) {
			fputc(' ', stderr);
			fputs(errlog_str[i], stderr);
			ret = -1;
		}
	}
	if (!ret)
		fputs(" none, everything completed cleanly", stderr);
	fputc('\n', stderr);
	if (g_shm->errR)
		fprintf(stderr, "read()/recv(): %s\n", strerror(g_shm->errR));
	if (g_shm->errW)
		fprintf(stderr, "write()/send(): %s\n", strerror(g_shm->errW));
	fputc('\n', stderr);
	buf_report_stats(g_buf);
	fputc('\n', stderr);

	return ret;
}

int main(int argc, char **argv)
{
	int ret = -1;

	if (common_init() < 0)
		goto out1;
	if (opt_parse(&g_opts, argc, argv) < 0)
		goto out1;
	if (setup_winsock() < 0)
		goto out1;
	do {
		if ((ret = setup_fds()) < 0)
			goto out2;
		if ((ret = setup_env()) < 0)
			goto out3;
		if ((ret = setup_proc()) < 0)
			goto out4;
		
		if (g_role == reader)
			task_reader("reader process");
		else if (g_role == writer)
			task_writer("writer process");
		else if (g_opts.mode == sp) /* implied arbiter */
			task_single("1cpu process");

		if (g_role == arbiter)
			ret = reaper();
out4:
		cleanup_env();
out3:
		cleanup_fds();
	} while (g_opts.loop && ret >= 0 && g_role == arbiter);
out2:
	cleanup_winsock();
out1:
	fflush(stderr);	/* this is mingwizm, otherwise it cuts out buffered output in console for w/e reasons ... */
	return ret;
}
