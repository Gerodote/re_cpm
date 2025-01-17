/**
 * @file icestr.c  ICE Strings
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mbuf.h>
#include <re/re_list.h>
#include <re/re_tmr.h>
#include <re/re_sa.h>
#include <re/re_stun.h>
#include <re/re_ice.h>
#include "ice.h"


const char *ice_cand_type2name(enum ice_cand_type type)
{
	switch (type) {

	case ICE_CAND_TYPE_HOST:  return "host";
	case ICE_CAND_TYPE_SRFLX: return "srflx";
	case ICE_CAND_TYPE_PRFLX: return "prflx";
	case ICE_CAND_TYPE_RELAY: return "relay";
	default:                  return "???";
	}
}


enum ice_cand_type ice_cand_name2type(const char *name)
{
	if (0 == str_casecmp(name, "host"))  return ICE_CAND_TYPE_HOST;
	if (0 == str_casecmp(name, "srflx")) return ICE_CAND_TYPE_SRFLX;
	if (0 == str_casecmp(name, "prflx")) return ICE_CAND_TYPE_PRFLX;
	if (0 == str_casecmp(name, "relay")) return ICE_CAND_TYPE_RELAY;

	return (enum ice_cand_type)-1;
}


const char *ice_role2name(enum ice_role role)
{
	switch (role) {

	case ICE_ROLE_UNKNOWN:     return "Unknown";
	case ICE_ROLE_CONTROLLING: return "Controlling";
	case ICE_ROLE_CONTROLLED:  return "Controlled";
	default:               return "???";
	}
}


const char *ice_candpair_state2name(enum ice_candpair_state st)
{
	switch (st) {

	case ICE_CANDPAIR_FROZEN:     return "Frozen";
	case ICE_CANDPAIR_WAITING:    return "Waiting";
	case ICE_CANDPAIR_INPROGRESS: return "InProgress";
	case ICE_CANDPAIR_SUCCEEDED:  return "Succeeded";
	case ICE_CANDPAIR_FAILED:     return "Failed";
	default:                      return "???";
	}
}


const char *ice_checkl_state2name(enum ice_checkl_state cst)
{
	switch (cst) {

	case ICE_CHECKLIST_NULL:      return "(NULL)";
	case ICE_CHECKLIST_RUNNING:   return "Running";
	case ICE_CHECKLIST_COMPLETED: return "Completed";
	case ICE_CHECKLIST_FAILED:    return "Failed";
	default:                      return "???";
	}
}
