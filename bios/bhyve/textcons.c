/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2020 Leon Dang
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

// Text console scraper.. for debugging purposes.

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#include <netinet/in.h>
#include <netdb.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "bhyverun.h"
#include "debug.h"
#include "console.h"
#include "sockstream.h"

//static int textcons_debug = 0;
#define	DPRINTF(params) if (textcons_debug) PRINTLN params
#define	WPRINTF(params) PRINTLN params

#define VERSION_LENGTH	12
#define AUTH_LENGTH	16
#define PASSWD_LENGTH	8

#define SECURITY_TYPE_NONE	1
#define SECURITY_TYPE_VNC_AUTH	2

#define AUTH_FAILED_UNAUTH	1
#define AUTH_FAILED_ERROR	2

struct textcons_softc {
	int		sfd;
	pthread_t	tid;

	int		cfd;

	int		width, height;
	char		*b8000_buf;
};

static int
textcons_send_screen(struct textcons_softc *rc, int cfd)
{
	char *buf = rc->b8000_buf;

	write(cfd, "\033[2J\n", 4);
	for (int y = 0; y < 25; y++) {
		dprintf(cfd, "[%02d] ", y);
		for (int x = 0; x < 80; x++) {
			if (write(cfd, buf, 1) < 0)
				return -1;
			buf += 2;
		}
		write(cfd, "\r\n", 2);
	}
	return (0);
}


static int
textcons_recv_key_msg(struct textcons_softc *rc, int cfd)
{
	char buf[16];
	int err;

	err = read(cfd, buf, sizeof(buf));
	if (err > 0)
		buf[err] = 0;

	printf("\r\n      TEXTCONS-KBD-BUF: %s(%x, %d)\r\n", buf, buf[0], err);

	console_key_event(1, buf[0]);
	usleep(50000);
	console_key_event(0, buf[0]);
	return err;
}

static int64_t
timeval_delta(struct timeval *prev, struct timeval *now)
{
	int64_t n1, n2;
	n1 = now->tv_sec * 1000000 + now->tv_usec;
	n2 = prev->tv_sec * 1000000 + prev->tv_usec;
	return (n1 - n2);
}

static void *
textcons_wr_thr(void *arg)
{
	struct textcons_softc *rc;
	int cfd;

	rc = arg;
	cfd = rc->cfd;

	while (rc->cfd >= 0) {
		/* Determine if its time to push screen; ~24hz */
		if (textcons_send_screen(rc, cfd) < 0) {
			return (NULL);
		}
		usleep(500000);
	}

	return (NULL);
}

void
textcons_handle(struct textcons_softc *rc, int cfd)
{
	pthread_t tid;
	int perror = 1;

	rc->cfd = cfd;

	perror = pthread_create(&tid, NULL, textcons_wr_thr, rc);
        if (perror == 0)
                pthread_set_name_np(tid, "textcons");

	for (;;) {
		if (textcons_recv_key_msg(rc, cfd) <= 0)
			break;
	}

	rc->cfd = -1;
	if (perror == 0)
		pthread_join(tid, NULL);
}

static void *
textcons_thr(void *arg)
{
	struct textcons_softc *rc;
	sigset_t set;

	int cfd;

	rc = arg;

	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
		perror("pthread_sigmask");
		return (NULL);
	}

	for (;;) {
		cfd = accept(rc->sfd, NULL, NULL);
		textcons_handle(rc, cfd);
		close(cfd);
	}


	/* NOTREACHED */
	return (NULL);
}

int
textcons_init(char *hostname, int port, void *guest_vga_buf)
{
	int e;
	char servname[6];
	struct textcons_softc *rc;
	struct addrinfo *ai = NULL;
	struct addrinfo hints;
	int on = 1;

	rc = calloc(1, sizeof(struct textcons_softc));
	rc->sfd = -1;

	snprintf(servname, sizeof(servname), "%d", port ? port : 5900);

	if (!hostname || strlen(hostname) == 0)
#if defined(INET)
		hostname = "127.0.0.1";
#elif defined(INET6)
		hostname = "[::1]";
#endif

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;

	if ((e = getaddrinfo(hostname, servname, &hints, &ai)) != 0) {
		EPRINTLN("getaddrinfo: %s", gai_strerror(e));
		goto error;
	}

	rc->sfd = socket(ai->ai_family, ai->ai_socktype, 0);
	if (rc->sfd < 0) {
		perror("socket");
		goto error;
	}

	setsockopt(rc->sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	if (bind(rc->sfd, ai->ai_addr, ai->ai_addrlen) < 0) {
		perror("bind");
		goto error;
	}

	if (listen(rc->sfd, 1) < 0) {
		perror("listen");
		goto error;
	}

	rc->b8000_buf = guest_vga_buf;

	pthread_create(&rc->tid, NULL, textcons_thr, rc);
	pthread_set_name_np(rc->tid, "textcons");

	freeaddrinfo(ai);
	return (0);

 error:
	if (ai != NULL)
		freeaddrinfo(ai);
	if (rc->sfd != -1)
		close(rc->sfd);
	free(rc);
	return (-1);
}
