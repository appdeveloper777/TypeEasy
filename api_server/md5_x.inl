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

#if defined(__GNUC__) && (__GNUC__ > 4)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif

#define GET_UINT32(p) (*(const md5_word_t *)(p))

#define PUT_UINT32(v, p)                                                       \
	{                                                                          \
		(*(md5_word_t *)(p)) = (v);                                             \
	}

#if defined(_WIN32)
/* The following code is for Microsoft C */
#define md5_byte_swap(x) _byteswap_ulong(x)
#elif defined(__APPLE__)
/* The following code is for Apple's OS X */
#include <libkern/OSByteOrder.h>
#define md5_byte_swap(x) OSSwapInt32(x)
#elif defined(__sun) || defined(__SVR4)
/* The following code is for SunOS */
#include <sys/byteorder.h>
#define md5_byte_swap(x) BSWAP_32(x)
#elif defined(__FreeBSD__)
/* The following code is for FreeBSD */
#include <sys/endian.h>
#define md5_byte_swap(x) bswap32(x)
#elif defined(__OpenBSD__)
/* The following code is for OpenBSD */
#include <sys/types.h>
#define md5_byte_swap(x) swap32(x)
#elif defined(__NetBSD__)
/* The following code is for NetBSD */
/*#include <sys/types.h>*/
/*#include <machine/bswap.h>*/
#if defined(__BSWAP_RENAME) && !defined(__bswap_32)
#define md5_byte_swap(x) bswap32(x)
#endif
#else
/* The following code is for GNU C */
#include <byteswap.h>
#define md5_byte_swap(x) bswap_32(x)
#endif

#if defined(md5_byte_swap)
#define X(i) (t = md5_byte_swap(GET_UINT32(data + i * 4)), x[i] = t)
#else
#define X(i)                                                                   \
	(t = ((md5_word_t)data[i * 4 + 3] << 24)                                    \
	         | ((md5_word_t)data[i * 4 + 2] << 16)                              \
	         | ((md5_word_t)data[i * 4 + 1] << 8)                               \
	         | ((md5_word_t)data[i * 4 + 0]),                                   \
	 x[i] = t)
#endif

#if defined(__GNUC__) && (__GNUC__ > 4)
#pragma GCC diagnostic pop
#endif

P(a, b, c, d, X(0), 7, 0xd76aa478);
P(d, a, b, c, X(1), 12, 0xe8c7b756);
P(c, d, a, b, X(2), 17, 0x242070db);
P(b, c, d, a, X(3), 22, 0xc1bdceee);
P(a, b, c, d, X(4), 7, 0xf57c0faf);
P(d, a, b, c, X(5), 12, 0x4787c62a);
P(c, d, a, b, X(6), 17, 0xa8304613);
P(b, c, d, a, X(7), 22, 0xfd469501);
P(a, b, c, d, X(8), 7, 0x698098d8);
P(d, a, b, c, X(9), 12, 0x8b44f7af);
P(c, d, a, b, X(10), 17, 0xffff5bb1);
P(b, c, d, a, X(11), 22, 0x895cd7be);
P(a, b, c, d, X(12), 7, 0x6b901122);
P(d, a, b, c, X(13), 12, 0xfd987193);
P(c, d, a, b, X(14), 17, 0xa679438e);
P(b, c, d, a, X(15), 22, 0x49b40821);

#undef x
#undef X

#define X(i) x[i]

P(a, b, c, d, X(1), 5, 0xf61e2562);
P(d, a, b, c, X(6), 9, 0xc040b340);
P(c, d, a, b, X(11), 14, 0x265e5a51);
P(b, c, d, a, X(0), 20, 0xe9b6c7aa);
P(a, b, c, d, X(5), 5, 0xd62f105d);
P(d, a, b, c, X(10), 9, 0x02441453);
P(c, d, a, b, X(15), 14, 0xd8a1e681);
P(b, c, d, a, X(4), 20, 0xe7d3fbc8);
P(a, b, c, d, X(9), 5, 0x21e1cde6);
P(d, a, b, c, X(14), 9, 0xc33707d6);
P(c, d, a, b, X(3), 14, 0xf4d50d87);
P(b, c, d, a, X(8), 20, 0x455a14ed);
P(a, b, c, d, X(13), 5, 0xa9e3e905);
P(d, a, b, c, X(2), 9, 0xfcefa3f8);
P(c, d, a, b, X(7), 14, 0x676f02d9);
P(b, c, d, a, X(12), 20, 0x8d2a4c8a);

P(a, b, c, d, X(5), 4, 0xfffa3942);
P(d, a, b, c, X(8), 11, 0x8771f681);
P(c, d, a, b, X(11), 16, 0x6d9d6122);
P(b, c, d, a, X(14), 23, 0xfde5380c);
P(a, b, c, d, X(1), 4, 0xa4beea44);
P(d, a, b, c, X(4), 11, 0x4bdecfa9);
P(c, d, a, b, X(7), 16, 0xf6bb4b60);
P(b, c, d, a, X(10), 23, 0xbebfbc70);
P(a, b, c, d, X(13), 4, 0x289b7ec6);
P(d, a, b, c, X(0), 11, 0xeaa127fa);
P(c, d, a, b, X(3), 16, 0xd4ef3085);
P(b, c, d, a, X(6), 23, 0x04881d05);
P(a, b, c, d, X(9), 4, 0xd9d4d039);
P(d, a, b, c, X(12), 11, 0xe6db99e5);
P(c, d, a, b, X(15), 16, 0x1fa27cf8);
P(b, c, d, a, X(2), 23, 0xc4ac5665);

P(a, b, c, d, X(0), 6, 0xf4292244);
P(d, a, b, c, X(7), 10, 0x432aff97);
P(c, d, a, b, X(14), 15, 0xab9423a7);
P(b, c, d, a, X(5), 21, 0xfc93a039);
P(a, b, c, d, X(12), 6, 0x655b59c3);
P(d, a, b, c, X(3), 10, 0x8f0ccc92);
P(c, d, a, b, X(10), 15, 0xffeff47d);
P(b, c, d, a, X(1), 21, 0x85845dd1);
P(a, b, c, d, X(8), 6, 0x6fa87e4f);
P(d, a, b, c, X(15), 10, 0xfe2ce6e0);
P(c, d, a, b, X(6), 15, 0xa3014314);
P(b, c, d, a, X(13), 21, 0x4e0811a1);
P(a, b, c, d, X(4), 6, 0xf7537e82);
P(d, a, b, c, X(11), 10, 0xbd3af235);
P(c, d, a, b, X(2), 15, 0x2ad7d2bb);
P(b, c, d, a, X(9), 21, 0xeb86d391);