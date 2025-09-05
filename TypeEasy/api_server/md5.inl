/*
 * This file is a part of the CivetWeb web server.
 * For detailed copyright information, please see the CivetWeb homepage
 * at https://github.com/civetweb/civetweb
 */

/*
 * This file is partly based on the work of:
 *   L. Peter Deutsch
 *   Colin Plumb
 * The original copyright of the RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm is included below.
 */

/*
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#ifndef MD5_INL_H
#define MD5_INL_H

#if !defined(MD5_LEGACY_INTERFACE)
/* The new interface is namespaced with "md5_" */

typedef unsigned char md5_byte_t; /* 8-bit byte */
typedef unsigned int md5_word_t;  /* 32-bit word */

/* Define the state of the MD5 Algorithm. */
typedef struct md5_state_s {
	md5_word_t count[2]; /* message length in bits, lsw first */
	md5_word_t abcd[4];  /* digest buffer */
	md5_byte_t buf[64];  /* accumulate block */
} md5_state_t;

#else
/* To use the old "global" interface, define MD5_LEGACY_INTERFACE.
 * This is not recommended. */

typedef unsigned char md5_byte_t; /* 8-bit byte */
typedef unsigned int md5_word_t;  /* 32-bit word */

typedef struct MD5_CTX {
	md5_word_t count[2]; /* message length in bits, lsw first */
	md5_word_t abcd[4];  /* digest buffer */
	md5_byte_t buf[64];  /* accumulate block */
} MD5_CTX;

#define md5_state_t MD5_CTX
#define md5_init MD5Init
#define md5_append MD5Update
#define md5_finish MD5Final

#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the algorithm. */
void md5_init(md5_state_t *pms);

/* Append a string to the message. */
void md5_append(md5_state_t *pms, const md5_byte_t *data, size_t nbytes);

/* Finish the message and return the digest. */
void md5_finish(md5_state_t *pms, md5_byte_t digest[16]);

#ifdef __cplusplus
} /* end extern "C" */
#endif

#endif /* !MD5_INL_H */


#if !defined(MD5_C_) && !defined(MD5_C)
#define MD5_C_
#define MD5_C

#include <string.h>

#undef S
#define S(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#undef P
#define P(a, b, c, d, k, s, t)                                                 \
	{                                                                          \
		(a) += F((b), (c), (d)) + (k) + (t);                                    \
		(a) = S((a), (s));                                                     \
		(a) += (b);                                                            \
	}

static void
md5_process(md5_state_t *pms, const md5_byte_t *data /*[64]*/)
{
	md5_word_t a, b, c, d;
	md5_word_t t;
	md5_word_t x[16];

#if defined(__GNUC__) && (__GNUC__ > 4)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

	a = pms->abcd[0];
	b = pms->abcd[1];
	c = pms->abcd[2];
	d = pms->abcd[3];

#define F(x, y, z) (((x) & (y)) | (~(x) & (z)))

#include "md5_x.inl"

#undef F
#define F(x, y, z) (((x) & (z)) | ((y) & ~(z)))

#include "md5_x.inl"

#undef F
#define F(x, y, z) ((x) ^ (y) ^ (z))

#include "md5_x.inl"

#undef F
#define F(x, y, z) ((y) ^ ((x) | ~(z)))

#include "md5_x.inl"

	pms->abcd[0] += a;
	pms->abcd[1] += b;
	pms->abcd[2] += c;
	pms->abcd[3] += d;

#if defined(__GNUC__) && (__GNUC__ > 4)
#pragma GCC diagnostic pop
#endif
}

void
md5_init(md5_state_t *pms)
{
	pms->count[0] = pms->count[1] = 0;
	pms->abcd[0] = 0x67452301;
	pms->abcd[1] = 0xefcdab89;
	pms->abcd[2] = 0x98badcfe;
	pms->abcd[3] = 0x10325476;
}

void
md5_append(md5_state_t *pms, const md5_byte_t *data, size_t nbytes)
{
	const md5_byte_t *p = data;
	size_t left = nbytes;
	size_t offset = (pms->count[0] >> 3) & 63;
	md5_word_t nbits = (md5_word_t)(nbytes << 3);

	if (nbytes == 0) {
		return;
	}

	pms->count[0] += nbits;
	pms->count[1] += (nbytes >> 29);

	if (offset) {
		size_t copy = (offset + nbytes > 64) ? (64 - offset) : nbytes;

		memcpy(pms->buf + offset, p, copy);
		if (offset + copy < 64) {
			return;
		}
		p += copy;
		left -= copy;
		md5_process(pms, pms->buf);
	}

	for (; left >= 64; p += 64, left -= 64) {
		md5_process(pms, p);
	}

	if (left) {
		memcpy(pms->buf, p, left);
	}
}

static const md5_byte_t md5_padding[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void
md5_finish(md5_state_t *pms, md5_byte_t digest[16])
{
	size_t offset = (pms->count[0] >> 3) & 63;
	size_t pad = (offset < 56) ? (56 - offset) : (120 - offset);
	md5_byte_t bits[8];
	int i;

	bits[0] = (md5_byte_t)(pms->count[0] & 0xFF);
	bits[1] = (md5_byte_t)((pms->count[0] >> 8) & 0xFF);
	bits[2] = (md5_byte_t)((pms->count[0] >> 16) & 0xFF);
	bits[3] = (md5_byte_t)((pms->count[0] >> 24) & 0xFF);
	bits[4] = (md5_byte_t)(pms->count[1] & 0xFF);
	bits[5] = (md5_byte_t)((pms->count[1] >> 8) & 0xFF);
	bits[6] = (md5_byte_t)((pms->count[1] >> 16) & 0xFF);
	bits[7] = (md5_byte_t)((pms->count[1] >> 24) & 0xFF);

	md5_append(pms, md5_padding, pad);
	md5_append(pms, bits, 8);

	for (i = 0; i < 4; i++) {
		digest[i] = (md5_byte_t)((pms->abcd[0] >> (i * 8)) & 0xFF);
		digest[i + 4] = (md5_byte_t)((pms->abcd[1] >> (i * 8)) & 0xFF);
		digest[i + 8] = (md5_byte_t)((pms->abcd[2] >> (i * 8)) & 0xFF);
		digest[i + 12] = (md5_byte_t)((pms->abcd[3] >> (i * 8)) & 0xFF);
	}
}

#endif /* MD5_C_ */