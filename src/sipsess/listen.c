/**
 * @file sipsess/listen.c  SIP Session Listen
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
	struct sipsess_sock *sock = arg;

	mem_deref(sock->lsnr_resp);
	mem_deref(sock->lsnr_req);
	hash_flush(sock->ht_sess);
	mem_deref(sock->ht_sess);
	hash_flush(sock->ht_ack);
	mem_deref(sock->ht_ack);
}


static void internal_connect_handler(const struct sip_msg *msg, void *arg)
{
	struct sipsess_sock *sock = arg;

	(void)sip_treply(NULL, sock->sip, msg, 486, "Busy Here");
}


static void info_handler(struct sipsess_sock *sock, const struct sip_msg *msg)
{
	struct sip *sip = sock->sip;
	struct sipsess *sess;

	sess = sipsess_find(sock, msg);
	if (!sess || sess->terminated) {
		(void)sip_reply(sip, msg, 481, "Call Does Not Exist");
		return;
	}

	if (!sip_dialog_rseq_valid(sess->dlg, msg)) {
		(void)sip_reply(sip, msg, 500, "Server Internal Error");
		return;
	}

	if (!sess->infoh) {
		(void)sip_reply(sip, msg, 501, "Not Implemented");
		return;
	}

	sess->infoh(sip, msg, sess->arg);
}


static void refer_handler(struct sipsess_sock *sock, const struct sip_msg *msg)
{
	struct sip *sip = sock->sip;
	struct sipsess *sess;

	sess = sipsess_find(sock, msg);
	if (!sess || sess->terminated) {
		(void)sip_reply(sip, msg, 481, "Call Does Not Exist");
		return;
	}

	if (!sip_dialog_rseq_valid(sess->dlg, msg)) {
		(void)sip_reply(sip, msg, 500, "Server Internal Error");
		return;
	}

	if (!sess->referh) {
		(void)sip_reply(sip, msg, 501, "Not Implemented");
		return;
	}

	sess->referh(sip, msg, sess->arg);
}


static void bye_handler(struct sipsess_sock *sock, const struct sip_msg *msg)
{
	struct sip *sip = sock->sip;
	struct sipsess *sess;

	sess = sipsess_find(sock, msg);
	if (!sess) {
		(void)sip_reply(sip, msg, 481, "Call Does Not Exist");
		return;
	}

	if (!sip_dialog_rseq_valid(sess->dlg, msg)) {
		(void)sip_reply(sip, msg, 500, "Server Internal Error");
		return;
	}

	(void)sip_treplyf(NULL, NULL, sip, msg, false, 200, "OK",
			  "%s"
			  "Content-Length: 0\r\n"
			  "\r\n",
			  sess->close_hdrs);

	sess->peerterm = true;

	if (sess->terminated)
		return;

	if (sess->st) {
		(void)sip_treply(&sess->st, sess->sip, sess->msg,
				 487, "Request Terminated");
	}

	sipsess_terminate(sess, ECONNRESET, NULL);
}


static void ack_handler(struct sipsess_sock *sock, const struct sip_msg *msg)
{
	struct sipsess *sess;
	int err = 0;

	sess = sipsess_find(sock, msg);
	if (!sess)
		return;

	if (sipsess_reply_ack(sess, msg))
		return;

	if (sess->terminated) {
		if (!sess->replyl.head) {
			sess->established = true;
			mem_deref(sess);
		}
		return;
	}

	if (sess->neg_state == SDP_NEG_LOCAL_OFFER) {
		if (!mbuf_get_left(msg->mb)) {
			sipsess_terminate(sess, EPROTO, NULL);
			return;
		}

		sess->neg_state = SDP_NEG_DONE;
		err = sess->answerh(msg, sess->arg);
	}

	if (sess->modify_pending && !sess->replyl.head)
		(void)sipsess_reinvite(sess, true);

	if (sess->established)
		return;

	sess->msg = mem_deref((void *)sess->msg);
	sess->established = true;

	if (err)
		sipsess_terminate(sess, err, NULL);
	else
		sess->estabh(msg, sess->arg);
}


static void prack_handler(struct sipsess_sock *sock, const struct sip_msg *msg)
{
	bool sdp;
	struct sipsess *sess;
	struct mbuf *desc = NULL;
	bool awaiting_prack = false;

	sess = sipsess_find(sock, msg);

	if (!sess || sipsess_reply_prack(sess, msg, &awaiting_prack)) {
		(void)sip_reply(sock->sip, msg, 481,
				"Transaction Does Not Exist");
		return;
	}

	if (sess->terminated) {
		if (!sess->replyl.head) {
			sess->established = true;
			mem_deref(sess);
		}

		return;
	}

	sdp = mbuf_get_left(msg->mb);

	if (awaiting_prack)
		--sess->prack_waiting_cnt;

	if (sess->neg_state == SDP_NEG_LOCAL_OFFER) {
		if (!sdp) {
			sipsess_terminate(sess, EPROTO, NULL);
			return;
		}

		sess->neg_state = SDP_NEG_DONE;
		(void)sess->answerh(msg, sess->arg);
	}
	else if (sess->neg_state == SDP_NEG_DONE && sdp) {
		sess->neg_state = SDP_NEG_REMOTE_OFFER;
		(void)sess->offerh(&desc, msg, sess->arg);
	}

	if (sess->prackh)
		sess->prackh(msg, sess->arg);

	(void)sipsess_reply_2xx(sess, msg, 200, "OK", desc, NULL, NULL);

	mem_deref(desc);
}


static void target_refresh_handler(struct sipsess_sock *sock,
			     const struct sip_msg *msg)
{
	struct sip *sip = sock->sip;
	bool is_invite;
	bool sdp;
	struct sipsess *sess;
	struct mbuf *desc = NULL;
	char m[256];
	int err;

	sess = sipsess_find(sock, msg);
	if (!sess || sess->terminated) {
		(void)sip_treply(NULL, sip, msg, 481, "Call Does Not Exist");
		return;
	}

	is_invite = !pl_strcmp(&msg->met, "INVITE");
	sdp = (mbuf_get_left(msg->mb) > 0);

	if (!sip_dialog_rseq_valid(sess->dlg, msg)) {
		(void)sip_treply(NULL, sip, msg, 500, "Server Internal Error");
		return;
	}

	if ((is_invite && sess->st)
	    || (sdp && sess->neg_state == SDP_NEG_LOCAL_OFFER)) {
		(void)sip_treplyf(NULL, NULL, sip, msg, false,
				  500, "Server Internal Error",
				  "Retry-After: 5\r\n"
				  "Content-Length: 0\r\n"
				  "\r\n");
		return;
	}

	if (is_invite && sess->req) {
		(void)sip_treply(NULL, sip, msg, 491, "Request Pending");
		return;
	}

	if (sdp && !sipsess_refresh_allowed(sess)) {
		(void)sip_reply(sip, msg, 488, "Not Acceptable Here");
		return;
	}

	if (is_invite || sdp) {
		sess->neg_state = sdp ? SDP_NEG_REMOTE_OFFER :
				  SDP_NEG_LOCAL_OFFER;
		err = sess->offerh(&desc, msg, sess->arg);
		if (err) {
			(void)sip_reply(sip, msg, 488,
					str_error(err, m, sizeof(m)));
			sess->neg_state = SDP_NEG_DONE;
			return;
		}
	}

	(void)sip_dialog_update(sess->dlg, msg);
	(void)sipsess_reply_2xx(sess, msg, 200, "OK", desc,
				NULL, NULL);

	/* pending modifications considered outdated;
	   sdp may have changed in above exchange */
	sess->desc = mem_deref(sess->desc);
	sess->modify_pending = false;
	tmr_cancel(&sess->tmr);
	mem_deref(desc);
}


static void invite_handler(struct sipsess_sock *sock,
			   const struct sip_msg *msg)
{
	sock->connh(msg, sock->arg);
}


static bool request_handler(const struct sip_msg *msg, void *arg)
{
	struct sipsess_sock *sock = arg;

	if (!pl_strcmp(&msg->met, "INVITE")) {

		if (pl_isset(&msg->to.tag))
			target_refresh_handler(sock, msg);
		else
			invite_handler(sock, msg);

		return true;
	}
	else if (!pl_strcmp(&msg->met, "UPDATE")) {
		target_refresh_handler(sock, msg);
		return true;
	}
	else if (!pl_strcmp(&msg->met, "ACK")) {
		ack_handler(sock, msg);
		return true;
	}
	else if (!pl_strcmp(&msg->met, "PRACK")) {
		prack_handler(sock, msg);
		return true;
	}
	else if (!pl_strcmp(&msg->met, "BYE")) {
		bye_handler(sock, msg);
		return true;
	}
	else if (!pl_strcmp(&msg->met, "INFO")) {
		info_handler(sock, msg);
		return true;
	}
	else if (!pl_strcmp(&msg->met, "REFER")) {

		if (!pl_isset(&msg->to.tag))
			return false;

		refer_handler(sock, msg);
		return true;
	}

	return false;
}


static bool response_handler(const struct sip_msg *msg, void *arg)
{
	struct sipsess_sock *sock = arg;

	if (pl_strcmp(&msg->cseq.met, "INVITE"))
		return false;

	if (msg->scode < 200 || msg->scode > 299)
		return false;

	(void)sipsess_ack_again(sock, msg);

	return true;
}


/**
 * Listen to a SIP Session socket for incoming connections
 *
 * @param sockp    Pointer to allocated SIP Session socket
 * @param sip      SIP Stack instance
 * @param htsize   Hashtable size
 * @param connh    Connection handler
 * @param arg      Handler argument
 *
 * @return 0 if success, otherwise errorcode
 */
int sipsess_listen(struct sipsess_sock **sockp, struct sip *sip,
		   int htsize, sipsess_conn_h *connh, void *arg)
{
	struct sipsess_sock *sock;
	int err;

	if (!sockp || !sip || !htsize)
		return EINVAL;

	sock = mem_zalloc(sizeof(*sock), destructor);
	if (!sock)
		return ENOMEM;

	err = sip_listen(&sock->lsnr_resp, sip, false, response_handler, sock);
	if (err)
		goto out;

	err = sip_listen(&sock->lsnr_req, sip, true, request_handler, sock);
	if (err)
		goto out;

	err = hash_alloc(&sock->ht_sess, htsize);
	if (err)
		goto out;

	err = hash_alloc(&sock->ht_ack, htsize);
	if (err)
		goto out;

	sock->sip   = sip;
	sock->connh = connh ? connh : internal_connect_handler;
	sock->arg   = connh ? arg : sock;

 out:
	if (err)
		mem_deref(sock);
	else
		*sockp = sock;

	return err;
}


/**
 * Close all SIP Sessions
 *
 * @param sock      SIP Session socket
 */
void sipsess_close_all(struct sipsess_sock *sock)
{
	if (!sock)
		return;

	hash_flush(sock->ht_sess);
}
