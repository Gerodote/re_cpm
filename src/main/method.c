/**
 * @file method.c  Polling methods
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mbuf.h>
#include <re/re_net.h>
#include <re/re_main.h>
#include "main.h"


static const char str_select[] = "select";   /**< POSIX.1-2001 select     */
static const char str_epoll[]  = "epoll";    /**< Linux epoll             */
static const char str_kqueue[] = "kqueue";


/**
 * Choose the best async I/O polling method
 *
 * @return Polling method
 */
enum poll_method poll_method_best(void)
{
#ifdef HAVE_EPOLL
	/* Supported from Linux 2.5.66 */
	return METHOD_EPOLL;
#endif

#ifdef HAVE_KQUEUE
	return METHOD_KQUEUE;
#endif

#ifdef HAVE_SELECT
	return METHOD_SELECT;
#endif

	return METHOD_NULL;
}


/**
 * Get the name of the polling method
 *
 * @param method Polling method
 *
 * @return Polling name string
 */
const char *poll_method_name(enum poll_method method)
{
	switch (method) {

	case METHOD_SELECT:    return str_select;
	case METHOD_EPOLL:     return str_epoll;
	case METHOD_KQUEUE:    return str_kqueue;
	default:               return "???";
	}
}


/**
 * Get the polling method type from a string
 *
 * @param method Returned polling method
 * @param name   Polling method name string
 *
 * @return 0 if success, otherwise errorcode
 */
int poll_method_type(enum poll_method *method, const struct pl *name)
{
	if (!method || !name)
		return EINVAL;

	if (0 == pl_strcasecmp(name, str_select))
		*method = METHOD_SELECT;
	else if (0 == pl_strcasecmp(name, str_epoll))
		*method = METHOD_EPOLL;
	else if (0 == pl_strcasecmp(name, str_kqueue))
		*method = METHOD_KQUEUE;
	else
		return ENOENT;

	return 0;
}
