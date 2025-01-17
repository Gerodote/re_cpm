/**
 * @file srtcp.c  Secure Real-time Transport Control Protocol (SRTCP)
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <re/re_types.h>
#include <re/re_fmt.h>
#include <re/re_mbuf.h>
#include <re/re_list.h>
#include <re/re_hmac.h>
#include <re/re_sha.h>
#include <re/re_aes.h>
#include <re/re_net.h>
#include <re/re_srtp.h>
#include "srtp.h"


static int get_rtcp_ssrc(uint32_t *ssrc, struct mbuf *mb)
{
	if (mbuf_get_left(mb) < 8)
		return EBADMSG;

	mbuf_advance(mb, 4);
	*ssrc = ntohl(mbuf_read_u32(mb));

	return 0;
}


int srtcp_encrypt(struct srtp *srtp, struct mbuf *mb)
{
	struct srtp_stream *strm;
	struct comp *rtcp;
	uint32_t ssrc;
	size_t start;
	uint32_t ep = 0;
	int err;

	if (!srtp || !mb)
		return EINVAL;

	rtcp = &srtp->rtcp;
	start = mb->pos;

	err = get_rtcp_ssrc(&ssrc, mb);
	if (err)
		return err;

	err = stream_get(&strm, srtp, ssrc);
	if (err)
		return err;

	strm->rtcp_index = (strm->rtcp_index+1) & 0x7fffffff;

	if (rtcp->aes && rtcp->mode == AES_MODE_CTR) {
		union vect128 iv;
		uint8_t *p = mbuf_buf(mb);

		srtp_iv_calc(&iv, &rtcp->k_s, ssrc, strm->rtcp_index);

		aes_set_iv(rtcp->aes, iv.u8);
		err = aes_encr(rtcp->aes, p, p, mbuf_get_left(mb));
		if (err)
			return err;

		ep = 1;
	}
	else if (rtcp->aes && rtcp->mode == AES_MODE_GCM) {

		union vect128 iv;
		uint8_t *p = mbuf_buf(mb);
		uint8_t tag[GCM_TAGLEN];
		const uint32_t ix_be = htonl(1L<<31 | strm->rtcp_index);

		srtp_iv_calc_gcm(&iv, &rtcp->k_s, ssrc, strm->rtcp_index);

		aes_set_iv(rtcp->aes, iv.u8);

		/* The RTCP Header and Index is Associated Data */
		err  = aes_encr(rtcp->aes, NULL, &mb->buf[start],
				mb->pos - start);
		err |= aes_encr(rtcp->aes, NULL,
				(void *)&ix_be, sizeof(ix_be));
		if (err)
			return err;

		err = aes_encr(rtcp->aes, p, p, mbuf_get_left(mb));
		if (err)
			return err;

		err = aes_get_authtag(rtcp->aes, tag, sizeof(tag));
		if (err)
			return err;

		mb->pos = mb->end;
		err = mbuf_write_mem(mb, tag, sizeof(tag));
		if (err)
			return err;

		ep = 1;
	}

	/* append E-bit and SRTCP-index */
	mb->pos = mb->end;
	err = mbuf_write_u32(mb, htonl(ep<<31 | strm->rtcp_index));
	if (err)
		return err;

	if (rtcp->hmac) {
		uint8_t tag[SHA_DIGEST_LENGTH] = {0};

		mb->pos = start;

		err = hmac_digest(rtcp->hmac, tag, sizeof(tag),
				  mbuf_buf(mb), mbuf_get_left(mb));
		if (err)
			return err;

		mb->pos = mb->end;

		err = mbuf_write_mem(mb, tag, rtcp->tag_len);
		if (err)
			return err;
	}

	mb->pos = start;

	return 0;
}


int srtcp_decrypt(struct srtp *srtp, struct mbuf *mb)
{
	size_t start, eix_start, pld_start;
	struct srtp_stream *strm;
	struct comp *rtcp;
	uint32_t v, ix;
	uint32_t ssrc;
	bool ep;
	int err;

	if (!srtp || !mb)
		return EINVAL;

	rtcp = &srtp->rtcp;
	start = mb->pos;

	err = get_rtcp_ssrc(&ssrc, mb);
	if (err)
		return err;

	err = stream_get(&strm, srtp, ssrc);
	if (err)
		return err;

	pld_start = mb->pos;

	if (mbuf_get_left(mb) < (4 + rtcp->tag_len))
		return EBADMSG;

	/* Read out E-Bit, SRTCP-index and Authentication Tag */
	eix_start = mb->end - (4 + rtcp->tag_len);
	mb->pos = eix_start;
	v = ntohl(mbuf_read_u32(mb));

	ep = (v >> 31) & 1;
	ix = v & 0x7fffffff;

	if (rtcp->hmac) {
		uint8_t tag[SHA_DIGEST_LENGTH] = {0};
		uint8_t tag_pkt[SHA_DIGEST_LENGTH] = {0};
		const size_t tag_start = mb->pos;

		if (rtcp->tag_len > SHA_DIGEST_LENGTH)
			return ERANGE;

		err = mbuf_read_mem(mb, tag_pkt, rtcp->tag_len);
		if (err)
			return err;

		mb->pos = start;
		mb->end = tag_start;

		err = hmac_digest(rtcp->hmac, tag, sizeof(tag),
				  mbuf_buf(mb), mbuf_get_left(mb));
		if (err)
			return err;

		if (0 != memcmp(tag, tag_pkt, rtcp->tag_len))
			return EAUTH;

		/*
		 * SRTCP replay protection is as defined in Section 3.3.2,
		 * but using the SRTCP index as the index i and a separate
		 * Replay List that is specific to SRTCP.
		 */
		if (!srtp_replay_check(&strm->replay_rtcp, ix))
			return EALREADY;
	}

	mb->end = eix_start;

	if (rtcp->aes && ep && rtcp->mode == AES_MODE_CTR) {
		union vect128 iv;
		uint8_t *p;

		mb->pos = pld_start;
		p = mbuf_buf(mb);

		srtp_iv_calc(&iv, &rtcp->k_s, ssrc, ix);

		aes_set_iv(rtcp->aes, iv.u8);
		err = aes_decr(rtcp->aes, p, p, mbuf_get_left(mb));
		if (err)
			return err;
	}
	else if (rtcp->aes && ep && rtcp->mode == AES_MODE_GCM) {

		union vect128 iv;
		size_t tag_start;
		uint8_t *p;

		srtp_iv_calc_gcm(&iv, &rtcp->k_s, ssrc, ix);

		aes_set_iv(rtcp->aes, iv.u8);

		/* The RTP Header is Associated Data */
		err  = aes_decr(rtcp->aes, NULL, &mb->buf[start],
				pld_start - start);
		err |= aes_decr(rtcp->aes, NULL, &mb->buf[eix_start], 4);
		if (err)
			return err;

		mb->pos = pld_start;
		p = mbuf_buf(mb);

		if (mbuf_get_left(mb) < GCM_TAGLEN)
			return EBADMSG;

		tag_start = mb->end - GCM_TAGLEN;

		err = aes_decr(rtcp->aes, p, p, tag_start - pld_start);
		if (err)
			return err;

		err = aes_authenticate(rtcp->aes, &mb->buf[tag_start],
				       GCM_TAGLEN);
		if (err)
			return err;

		mb->end = tag_start;
	}

	mb->pos = start;

	return 0;
}
