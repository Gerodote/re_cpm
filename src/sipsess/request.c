/**
 * @file sipsess/request.c  SIP Session Non-INVITE Request
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re/re_types.h>
#include <re/re_mem.h>
#include <re/re_mbuf.h>
#include <re/re_sa.h>
#include <re/re_list.h>
#include <re/re_hash.h>
#include <re/re_fmt.h>
#include <re/re_uri.h>
#include <re/re_tmr.h>
#include <re/re_msg.h>
#include <re/re_sip.h>
#include <re/re_sipsess.h>
#include "sipsess.h"


static void destructor(void *arg)
{
	struct sipsess_request *req = arg;

	tmr_cancel(&req->tmr);
	list_unlink(&req->le);
	mem_deref(req->ctype);
	mem_deref(req->body);
	mem_deref(req->req);

	/* wait for pending requests */
	if (req->sess->terminated && !req->sess->requestl.head)
		mem_deref(req->sess);
}


static void internal_resp_handler(int err, const struct sip_msg *msg,
				  void *arg)
{
	(void)err;
	(void)msg;
	(void)arg;
}


int sipsess_request_alloc(struct sipsess_request **reqp, struct sipsess *sess,
			  const char *ctype, struct mbuf *body,
			  sip_resp_h *resph, void *arg)
{
	struct sipsess_request *req;
	int err = 0;

	if (!reqp || !sess || sess->terminated)
		return EINVAL;

	req = mem_zalloc(sizeof(*req), destructor);
	if (!req)
		return ENOMEM;

	list_append(&sess->requestl, &req->le, req);

	if (ctype) {
		err = str_dup(&req->ctype, ctype);
		if (err)
			goto out;
	}

	req->sess  = sess;
	req->body  = mem_ref(body);
	req->resph = resph ? resph : internal_resp_handler;
	req->arg   = arg;
	tmr_init(&req->tmr);

 out:
	if (err)
		mem_deref(req);
	else
		*reqp = req;

	return err;
}
