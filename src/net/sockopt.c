/**
 * @file sockopt.c  Networking socket options
 *
 * Copyright (C) 2010 Creytiv.com
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_net.h>


#define DEBUG_MODULE "sockopt"
#define DEBUG_LEVEL 5
#include <re/re_dbg.h>


/** Platform independent buffer type cast */
#ifdef WIN32
#define BUF_CAST (char *)
#else
#define BUF_CAST
#endif


/**
 * Set socket option blocking or non-blocking
 *
 * @param fd       Socket file descriptor
 * @param blocking true for blocking, false for non-blocking
 *
 * @return 0 if success, otherwise errorcode
 */
int net_sockopt_blocking_set(re_sock_t fd, bool blocking)
{
#ifdef WIN32
	unsigned long noblock = !blocking;
	int err = 0;

	if (0 != ioctlsocket(fd, FIONBIO, &noblock)) {
		err = WSAGetLastError();
		DEBUG_WARNING("nonblock set: fd=%d err=%d (%m)\n",
			      fd, err, err);
	}
	return err;
#else
	int flags;
	int err = 0;

	flags = fcntl(fd, F_GETFL);
	if (-1 == flags) {
		err = errno;
		DEBUG_WARNING("sockopt set: fnctl F_GETFL: (%m)\n", err);
		goto out;
	}

	if (blocking)
		flags &= ~O_NONBLOCK;
	else
		flags |= O_NONBLOCK;

	if (-1 == fcntl(fd, F_SETFL, flags)) {
		err = errno;
		DEBUG_WARNING("sockopt set: fcntl F_SETFL non-block (%m)\n",
			      err);
	}

 out:
	return err;
#endif
}


/**
 * Set socket option to reuse address and port
 *
 * @param fd     Socket file descriptor
 * @param reuse  true for reuse, false for no reuse
 *
 * @return 0 if success, otherwise errorcode
 */
int net_sockopt_reuse_set(re_sock_t fd, bool reuse)
{
	int r = reuse;

#ifdef SO_REUSEADDR
	if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			     BUF_CAST &r, sizeof(r))) {
		DEBUG_WARNING("SO_REUSEADDR: %m\n", errno);
		return errno;
	}
#endif

#ifdef SO_REUSEPORT
	if (-1 == setsockopt(fd, SOL_SOCKET, SO_REUSEPORT,
			     BUF_CAST &r, sizeof(r))) {
		DEBUG_INFO("SO_REUSEPORT: %m\n", errno);
		return errno;
	}
#endif

#if !defined(SO_REUSEADDR) && !defined(SO_REUSEPORT)
	(void)r;
	(void)fd;
	(void)reuse;
	return ENOSYS;
#else
	return 0;
#endif
}


/**
 * Set socket IPV6_V6ONLY option (not supported on OpenBSD - readonly)
 *
 * @param fd     Socket file descriptor
 * @param only   true for IPv6 only, false for dual socket
 *
 * @return 0 if success, otherwise errorcode
 */
int net_sockopt_v6only(re_sock_t fd, bool only)
{
	int on = only;

#ifndef OPENBSD
#ifdef IPV6_V6ONLY
	if (-1 == setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY,
			     BUF_CAST &on, sizeof(on))) {
		int err = RE_ERRNO_SOCK;
		DEBUG_WARNING("IPV6_V6ONLY: %m\n", err);
		return err;
	}
#endif
#endif
	return 0;
}

