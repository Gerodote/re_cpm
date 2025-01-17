/**
 * @file ice/util.c  ICE Utilities
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifndef WIN32
#include <time.h>
#endif
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mem.h>
#include <re/re_mbuf.h>
#include <re/re_list.h>
#include <re/re_tmr.h>
#include <re/re_sa.h>
#include <re/re_stun.h>
#include <re/re_sys.h>
#include <re/re_ice.h>
#include "ice.h"


#define DEBUG_MODULE "iceutil"
#define DEBUG_LEVEL 5
#include <re/re_dbg.h>


enum {
	CAND_PRIO_RELAY =   0,
	CAND_PRIO_SRFLX = 100,
	CAND_PRIO_PRFLX = 110,
	CAND_PRIO_HOST  = 126
};


static uint32_t type_prio(enum ice_cand_type type)
{
	switch (type) {

	case ICE_CAND_TYPE_HOST:   return CAND_PRIO_HOST;
	case ICE_CAND_TYPE_SRFLX:  return CAND_PRIO_SRFLX;
	case ICE_CAND_TYPE_PRFLX:  return CAND_PRIO_PRFLX;
	case ICE_CAND_TYPE_RELAY:  return CAND_PRIO_RELAY;
	default: return 0;
	}
}


uint32_t ice_cand_calc_prio(enum ice_cand_type type, uint16_t lpref,
			    unsigned compid)
{
	return type_prio(type)<<24 | (uint32_t)lpref<<8 | (256 - compid);
}


/*
 * g = controlling agent
 * d = controlled agent

 pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)

 */
uint64_t ice_calc_pair_prio(uint32_t g, uint32_t d)
{
	const uint64_t m = min(g, d);
	const uint64_t x = max(g, d);

	return (m<<32) + 2*x + (g>d?1:0);
}


void ice_switch_local_role(struct icem *icem)
{
	enum ice_role new_role;

	if (ICE_ROLE_CONTROLLING == icem->lrole)
		new_role = ICE_ROLE_CONTROLLED;
	else
		new_role = ICE_ROLE_CONTROLLING;

	DEBUG_NOTICE("Switch local role from %s to %s\n",
		     ice_role2name(icem->lrole), ice_role2name(new_role));

	icem->lrole = new_role;

#if 0
	/* recompute pair priorities for all media streams */
	for (le = icem->le.list->head; le; le = le->next) {
		icem = le->data;
		icem_candpair_prio_order(&icem->checkl);
	}
#endif
}


/**
 * Remove duplicate elements from list, preserving order
 *
 * @param list  Linked list
 * @param uh    Unique handler (return object to remove)
 *
 * @return Number of elements removed
 *
 * @note:    O (n ^ 2)
 */
uint32_t ice_list_unique(struct list *list, list_unique_h *uh)
{
	struct le *le1 = list_head(list);
	uint32_t n = 0;

	while (le1 && le1 != list->tail) {

		struct le *le2 = le1->next;
		void *data = NULL;

		while (le2) {

			data = uh(le1, le2);

			le2 = le2->next;

			if (!data)
				continue;

			if (le1->data == data)
				break;
			else {
				data = mem_deref(data);
				++n;
			}
		}

		le1 = le1->next;

		if (data) {
			mem_deref(data);
			++n;
		}
	}

	return n;
}
