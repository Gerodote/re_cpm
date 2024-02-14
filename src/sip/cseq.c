/**
 * @file cseq.c  SIP CSeq decode
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mbuf.h>
#include <re/re_uri.h>
#include <re/re_list.h>
#include <re/re_sa.h>
#include <re/re_msg.h>
#include <re/re_sip.h>


/**
 * Decode a pointer-length string into a SIP CSeq header
 *
 * @param cseq SIP CSeq header
 * @param pl   Pointer-length string
 *
 * @return 0 for success, otherwise errorcode
 */
int sip_cseq_decode(struct sip_cseq *cseq, const struct pl *pl)
{
	struct pl num;
	int err;

	if (!cseq || !pl)
		return EINVAL;

	err = re_regex(pl->p, pl->l, "[0-9]+[ \t\r\n]+[^ \t\r\n]+",
		       &num, NULL, &cseq->met);
	if (err)
		return err;

	cseq->num = pl_u32(&num);

	return 0;
}
