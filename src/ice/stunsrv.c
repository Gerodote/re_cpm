/**
 * @file stunsrv.c  Basic STUN Server for Connectivity checks
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mem.h>
#include <re/re_mbuf.h>
#include <re/re_list.h>
#include <re/re_tmr.h>
#include <re/re_sa.h>
#include <re/re_stun.h>
#include <re/re_ice.h>
#include <re/re_sys.h>
#include "ice.h"


#define DEBUG_MODULE "stunsrv"
#define DEBUG_LEVEL 5
#include <re/re_dbg.h>


static const char *sw = "ice stunsrv v" RE_VERSION " (" ARCH "/" OS ")";


static void triggered_check(struct icem *icem, struct ice_cand *lcand,
			    struct ice_cand *rcand)
{
	struct ice_candpair *cp = NULL;
	int err;

	if (lcand && rcand)
		cp = icem_candpair_find(&icem->checkl, lcand, rcand);

	if (cp) {

		switch (cp->state) {

#if 0
			/* TODO: I am not sure why we should cancel the
			 *       pending Connectivity check here. this
			 *       can lead to a deadlock situation where
			 *       both agents are stuck on sending
			 *       triggered checks on the same candidate pair
			 */
		case ICE_CANDPAIR_INPROGRESS:
			icem_candpair_cancel(cp);
			/*@fallthrough@*/
#endif

		case ICE_CANDPAIR_FAILED:
			icem_candpair_set_state(cp, ICE_CANDPAIR_WAITING);
			/*@fallthrough@*/

		case ICE_CANDPAIR_FROZEN:
		case ICE_CANDPAIR_WAITING:
			err = icem_conncheck_send(cp, false, true);
			if (err) {
				DEBUG_WARNING("triggered check failed\n");
			}
			break;

		case ICE_CANDPAIR_SUCCEEDED:
		default:
			break;
		}
	}
	else {

#if 0
		err = icem_candpair_alloc(&cp, icem, lcand, rcand);
		if (err) {
			DEBUG_WARNING("failed to allocate candpair:"
				      " lcand=%p rcand=%p (%m)\n",
				      lcand, rcand, err);
			return;
		}

		icem_candpair_prio_order(&icem->checkl);

		icem_candpair_set_state(cp, ICE_CANDPAIR_WAITING);

		(void)icem_conncheck_send(cp, false, true);
#endif

	}
}


/*
 * 7.2.1.  Additional Procedures for Full Implementations
 */
static int handle_stun_full(struct icem *icem,
			    struct icem_comp *comp, const struct sa *src,
			    uint32_t prio, bool use_cand, bool tunnel)
{
	struct ice_cand *lcand = NULL, *rcand;
	struct ice_candpair *cp = NULL;
	int err;

	rcand = icem_cand_find(&icem->rcandl, comp->id, src);
	if (!rcand) {
		err = icem_rcand_add_prflx(&rcand, icem, comp->id, prio, src);
		if (err)
			return err;
	}

	cp = icem_candpair_find_rcand(icem, rcand);
	if (cp)
		lcand = cp->lcand;
	else
		lcand = icem_lcand_find_checklist(icem, comp->id);

	if (!lcand) {
		DEBUG_NOTICE("{%s.%u} local candidate not found"
			     " (checklist=%u) (src=%J)\n",
			     icem->name, comp->id,
			     list_count(&icem->checkl), src);
		return 0;
	}

	triggered_check(icem, lcand, rcand);

	if (!cp) {
		cp = icem_candpair_find_rcand(icem, rcand);
		if (!cp) {
			DEBUG_WARNING("{%s.%u} candidate pair not found:"
				      " source=%J\n",
				      icem->name, comp->id, src);
			return 0;
		}
	}

#if ICE_TRACE
	icecomp_printf(comp, "Rx Binding Request from %J via %s"
		       " (candpair=%s) %s\n",
		       src, tunnel ? "Tunnel" : "Socket",
		       ice_candpair_state2name(cp->state),
		       use_cand ? "[USE]" : "");
#else
	(void)tunnel;
#endif

	/* 7.2.1.5.  Updating the Nominated Flag */
	if (use_cand) {
		if (icem->lrole == ICE_ROLE_CONTROLLED &&
		    cp->state == ICE_CANDPAIR_SUCCEEDED) {

			if (!cp->nominated) {
				icecomp_printf(comp, "setting NOMINATED"
					       " flag on candpair [%H]\n",
					       icem_candpair_debug, cp);
			}

			cp->nominated = true;
		}
	}

	return 0;
}


static int stunsrv_ereply(struct icem_comp *comp, const struct sa *src,
			  size_t presz, const struct stun_msg *req,
			  uint16_t scode, const char *reason)
{
	struct icem *icem = comp->icem;

	return stun_ereply(icem->proto, comp->sock, src, presz, req,
			   scode, reason,
			   (uint8_t *)icem->lpwd, strlen(icem->lpwd), true, 1,
			   STUN_ATTR_SOFTWARE, sw);
}


int icem_stund_recv(struct icem_comp *comp, const struct sa *src,
		    struct stun_msg *req, size_t presz)
{
	struct icem *icem = comp->icem;
	struct stun_attr *attr;
	struct pl lu, ru;
	enum ice_role rrole = ICE_ROLE_UNKNOWN;
	uint64_t tiebrk = 0;
	uint32_t prio_prflx;
	bool use_cand = false;
	int err;

	/* RFC 5389: Fingerprint errors are silently discarded */
	err = stun_msg_chk_fingerprint(req);
	if (err)
		return err;

	err = stun_msg_chk_mi(req, (uint8_t *)icem->lpwd, strlen(icem->lpwd));
	if (err) {
		if (err == EBADMSG)
			goto unauth;
		else
			goto badmsg;
	}

	attr = stun_msg_attr(req, STUN_ATTR_USERNAME);
	if (!attr)
		goto badmsg;

	err = re_regex(attr->v.username, strlen(attr->v.username),
		       "[^:]+:[^]+", &lu, &ru);
	if (err) {
		DEBUG_WARNING("could not parse USERNAME attribute (%s)\n",
			      attr->v.username);
		goto unauth;
	}
	if (pl_strcmp(&lu, icem->lufrag))
		goto unauth;
	if (str_isset(icem->rufrag) && pl_strcmp(&ru, icem->rufrag))
		goto unauth;

	attr = stun_msg_attr(req, STUN_ATTR_CONTROLLED);
	if (attr) {
		rrole = ICE_ROLE_CONTROLLED;
		tiebrk = attr->v.uint64;
	}

	attr = stun_msg_attr(req, STUN_ATTR_CONTROLLING);
	if (attr) {
		rrole = ICE_ROLE_CONTROLLING;
		tiebrk = attr->v.uint64;
	}

	if (rrole == icem->lrole) {
		if (icem->tiebrk >= tiebrk)
			ice_switch_local_role(icem);
		else
			goto conflict;
	}

	attr = stun_msg_attr(req, STUN_ATTR_PRIORITY);
	if (attr)
		prio_prflx = attr->v.uint32;
	else
		goto badmsg;

	attr = stun_msg_attr(req, STUN_ATTR_USE_CAND);
	if (attr) {
		use_cand = true;
	}

	if (rrole == ICE_ROLE_CONTROLLED && use_cand) {
		DEBUG_NOTICE("remote peer is Controlled and"
			      " should not send USE-CANDIDATE\n");
	}

	err = handle_stun_full(icem, comp, src, prio_prflx,
			       use_cand, presz > 0);

	if (err)
		goto badmsg;

	return stun_reply(icem->proto, comp->sock, src, presz, req,
			  (uint8_t *)icem->lpwd, strlen(icem->lpwd), true, 2,
			  STUN_ATTR_XOR_MAPPED_ADDR, src,
			  STUN_ATTR_SOFTWARE, sw);

 badmsg:
	return stunsrv_ereply(comp, src, presz, req, 400, "Bad Request");

 unauth:
	return stunsrv_ereply(comp, src, presz, req, 401, "Unauthorized");

 conflict:
	return stunsrv_ereply(comp, src, presz, req, 487, "Role Conflict");
}
