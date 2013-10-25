/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * We keep our own copy of this algorithm for 3 main reasons:
 *        1. If we didn't, anyone modifying common/os/compress.c would
 *         directly break our on disk format
 *        2. Our version of lzjb does not have a number of checks that the
 *         common/os version needs and uses
 *        3. We initialize the lempel to ensure deterministic results,
 *           so that identical blocks can always be deduplicated.
 * In particular, we are adding the "feature" that compress() can
 * take a destination buffer size and returns the compressed length, or the
 * source length if compression would overflow the destination buffer.
 */


#include <sys/zfs_context.h>


#define        MATCH_BITS        6
#define        MATCH_MIN        3
#define        MATCH_MAX        ((1 << MATCH_BITS) + (MATCH_MIN - 1))
#define        OFFSET_MASK        ((1 << (16 - MATCH_BITS)) - 1)
#define        LEMPEL_SIZE        1024

/*ARGSUSED*/
#ifdef KERN_DEOPT
size_t
lzjb_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
__attribute__ ((__target__ ("no-mmx,no-sse")));
#endif
size_t
lzjb_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        uchar_t *src = s_start;
        uchar_t *dst = d_start;
        uchar_t *cpy, *copymap = NULL;
        int copymask = 1 << (NBBY - 1);
        int mlen, offset, hash;
        uint16_t *hp;
        uint16_t *lempel;

        lempel = kmem_zalloc(LEMPEL_SIZE * sizeof (uint16_t), KM_PUSHPAGE);
        while (src < (uchar_t *)s_start + s_len) {
                if ((copymask <<= 1) == (1 << NBBY)) {
                        if (dst >= (uchar_t *)d_start + d_len - 1 - 2 * NBBY) {
                                kmem_free(lempel, LEMPEL_SIZE*sizeof(uint16_t));
                                return (s_len);
                        }
                        copymask = 1;
                        copymap = dst;
                        *dst++ = 0;
                }
                if (src > (uchar_t *)s_start + s_len - MATCH_MAX) {
                        *dst++ = *src++;
                        continue;
                }
                hash = (src[0] << 16) + (src[1] << 8) + src[2];
                hash += hash >> 9;
                hash += hash >> 5;
                hp = &lempel[hash & (LEMPEL_SIZE - 1)];
                offset = (intptr_t)(src - *hp) & OFFSET_MASK;
                *hp = (uint16_t)(uintptr_t)src;
                cpy = src - offset;
                if (cpy >= (uchar_t *)s_start && cpy != src &&
                    src[0] == cpy[0] && src[1] == cpy[1] && src[2] == cpy[2]) {
                        *copymap |= copymask;
                        for (mlen = MATCH_MIN; mlen < MATCH_MAX; mlen++)
                                if (src[mlen] != cpy[mlen])
                                        break;
                        *dst++ = ((mlen - MATCH_MIN) << (NBBY - MATCH_BITS)) |
                            (offset >> NBBY);
                        *dst++ = (uchar_t)offset;
                        src += mlen;
                } else {
                        *dst++ = *src++;
                }
        }

        kmem_free(lempel, LEMPEL_SIZE * sizeof (uint16_t));
        return (dst - (uchar_t *)d_start);
}


/*ARGSUSED*/
#ifdef KERN_DEOPT
int
lzjb_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
__attribute__ ((__target__ ("no-mmx,no-sse")));
#endif
int
lzjb_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        uchar_t *src = s_start;
        uchar_t *dst = d_start;
        uchar_t *d_end = (uchar_t *)d_start + d_len;
        uchar_t *cpy, copymap = 0;
        int copymask = 1 << (NBBY - 1);

        while (dst < d_end) {
                if ((copymask <<= 1) == (1 << NBBY)) {
                        copymask = 1;
                        copymap = *src++;
                }
                if (copymap & copymask) {
                        int mlen = (src[0] >> (NBBY - MATCH_BITS)) + MATCH_MIN;
                        int offset = ((src[0] << NBBY) | src[1]) & OFFSET_MASK;
                        src += 2;
                        if ((cpy = dst - offset) < (uchar_t *)d_start)
                                return (-1);
                        while (--mlen >= 0 && dst < d_end)
                                *dst++ = *cpy++;
                } else {
                        *dst++ = *src++;
                }
        }

        /* Test Code to check all source is used. NOT ZFS CODE */
        if ((s_start + s_len) != src)
          return ((void*)src-(void*)s_start);

        return (0);
}

/*** Modified versions for testing are below here ***/
/*ARGSUSED*/
#ifdef KERN_DEOPT
int
lzjb_decompress_bsd(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
__attribute__ ((__target__ ("no-mmx,no-sse")));
#endif
int
lzjb_decompress_bsd(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        uchar_t *src = s_start;
        uchar_t *dst = d_start;
        uchar_t *d_end = (uchar_t *)d_start + d_len;
        uchar_t *cpy, copymap = 0;
        int copymask = 1 << (NBBY - 1);

        while (dst < d_end) {
                if ((copymask <<= 1) == (1 << NBBY)) {
                        copymask = 1;
                        copymap = *src++;
                }
                if (copymap & copymask) {
                        int mlen = (src[0] >> (NBBY - MATCH_BITS)) + MATCH_MIN;
                        int offset = ((src[0] << NBBY) | src[1]) & OFFSET_MASK;
                        src += 2;
                        if ((cpy = dst - offset) < (uchar_t *)d_start)
                                return (-1);
                        if (mlen > (d_end - dst))
                                 mlen = d_end - dst;
                        while (--mlen >= 0)
                                 *dst++ = *cpy++;

                } else {
                        *dst++ = *src++;
                }
        }

        /* Test Code to check all source is used. NOT ZFS CODE */
        if ((s_start + s_len) != src)
          return ((void*)src-(void*)s_start);

        return (0);
}

/****** Hacked or new versions of compression and decompression here on down. *******/
/*
 * This is the LZJB bitstream:
 * <copymap [8 bit] >
 * Starting from LSbit of copymap.
 * bit = 1?
 *   < 16 bit mlen:offset > ( MSByte first )
 *   mlen = 6 most significant bits + 3.
 *   offset = 10 least signicant bits.
 *     Copy mlen bytes from here - offset  to here.
 * bit = 0?
 *   output a literal byte.
 * repeat.
 *
 * This is completely re-written.  No code from standard lzjb is used.
 * Some variable names are preserved to ease code review.
 * Borrows some techniques or speed ideas from LZ4, but not its code.
 * License:
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE X CONSORTIUM BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Copyright: Steven Johnson, 2013.
 *
 */

#ifdef SHOWSTATS
typedef struct {
    uint64_t c8;
    uint64_t r1;
    uint64_t r2;
    uint64_t r3;
    uint64_t r4;
    uint64_t r5;
    uint64_t r6;
    uint64_t r7;
    uint64_t r8;
    uint64_t q1;
    uint64_t q2;
    uint64_t l1;
} decostats;

static decostats dstats = {0,0,0,0,0,0,0,0,0,0,0,0};
#endif

/* Assumption STEPSIZE >= 4 */
typedef struct {uint64_t v;} __attribute__ ((packed)) uint64_t_S;
typedef struct {uint32_t v;} __attribute__ ((packed)) uint32_t_S;
typedef struct {uint16_t v;} __attribute__ ((packed)) uint16_t_S;
#define A8BYTES(x)                (((uint64_t_S *)(x))->v)
#define A4BYTES(x)                (((uint32_t_S *)(x))->v)
#define A2BYTES(x)                (((uint16_t_S *)(x))->v)

#define STEPSIZE                  sizeof(uint64_t_S)
#define LZJB_COPYSTEP(d,s)        { A8BYTES(d) = A8BYTES(s); d+=STEPSIZE; s+=STEPSIZE; }
#define LZJB_QUICKCOPY(d,s,e)     { do { LZJB_COPYSTEP(d,s) } while (d<e);  }
#define LZJB_SLOWCOPY(d,s,e)      { while (d<e) { *d++ = *s++; } }

#ifdef KERN_DEOPT
int
lzjb_decompress_hack(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
__attribute__ ((__target__ ("no-mmx,no-sse")));
#endif
int
lzjb_decompress_hack(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        uchar_t *src = s_start;
        uchar_t *dst = d_start;
        uchar_t *d_end = (uchar_t *)d_start + d_len;
        uchar_t *d_safe_end = (uchar_t *)d_end - STEPSIZE;
/*        uchar_t *cpy;*/
        uchar_t *cpy_s;
        uchar_t *cpy_e;
        uint16_t mlen;
        size_t   offset;
        int      copymap = 0;
        int      mapbit;
        uint64_t r;
        uint32_t r2;
        uchar_t  r3;

        while (dst < d_end) {
            r = (void*)dst - (void*)d_start;
            /* 169b */
            /*
            if ((r >= 0x169b) && (r <= 0x16b5))
              r = 0; */ /* Put a breakpoint here */

            copymap = *src++;

            if (copymap == 0x00) { /* Copy ALL literals */
                if (dst <= d_safe_end) {
                  LZJB_COPYSTEP(dst,src);
                } else {
                  LZJB_SLOWCOPY(dst,src,d_end); /* Copies whats left */
                }
            } else {
                for (mapbit = 0; mapbit < NBBY; mapbit++) {
                    if (copymap & (0x01<<mapbit)) { /* Copy "compressed" region */

                        /* get the offset to copy from and the number of *
                         * matched bytes to copy.
                         */
                        offset =  (src[0] << NBBY) | src[1];
                        mlen   =  (offset >> ((2*NBBY) - MATCH_BITS)) + MATCH_MIN;
                        offset &= OFFSET_MASK;
                        src += 2;

                        /* calculate the source to copy from, and make sure its in range */
                        cpy_s = dst - offset;
                        if (cpy_s < (uchar_t *)d_start) return (-1);

                        /* we will be copying up to here */
                        cpy_e = dst + mlen;

                        /* Special Case which is effectively RLE (1-8 bytes wide) */
                        /* TODO: This may also be faster for copies that are ONLY this long */
                        /*    ^^ This is unimplemented and untested */
                        if (offset <= STEPSIZE) {
                            switch(offset) {
                              case 1 :
                                r = *cpy_s;
                                r = r | r << 8;
                                r = r | r << 16;
                                r = r | r << 32;
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    dst += STEPSIZE;
                                }
                                break;
                              case 2 :
                                r = A2BYTES(cpy_s);
                                r = r | r << 16;
                                r = r | r << 32;
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    dst += STEPSIZE;
                                }
                                break;
                              case 3 :
                                r = A2BYTES(cpy_s);
                                r2 = cpy_s[2];
                                while (dst < cpy_e) {
                                    A2BYTES(dst) = r;
                                    dst[2] = r2;
                                    dst += 3;
                                }
                                break;
                              case 4 :
                                r = A4BYTES(cpy_s);
                                r = r | r << 32;
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    dst += STEPSIZE;
                                }
                                break;
                              case 5 :
                                r = A4BYTES(cpy_s);
                                r2 = cpy_s[4];
                                while (dst < cpy_e) {
                                    A4BYTES(dst) = r;
                                    dst[4] = r2;
                                    dst += 5;
                                }
                                break;
                              case 6 :
                                r = A4BYTES(cpy_s);
                                r2 = A2BYTES(cpy_s+4);
                                while (dst < cpy_e) {
                                    A4BYTES(dst) = r;
                                    A2BYTES(dst+4) = r2;
                                    dst += 6;
                                }
                                break;
                              case 7 :
                                r  = A4BYTES(cpy_s);
                                r2 = A2BYTES(cpy_s+4);
                                r3 = cpy_s[6];
                                while (dst < cpy_e) {
                                    A4BYTES(dst) = r;
                                    A2BYTES(dst+4) = r2;
                                    dst[6] = r3;
                                    dst += 7;
                                }
                                break;
                              case 8 :
                                r = A8BYTES(cpy_s);
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    dst += STEPSIZE;
                                }
                                break;
#ifdef NOT_TESTED_YET
                                /* New RLE Options */
                              case 9 :
                                r = A8BYTES(cpy_s);
                                r2 = cpy_s[8];
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    dst[8] = r2;
                                    dst += (STEPSIZE+1);
                                }
                                break;
                              case 10 :
                                r = A8BYTES(cpy_s);
                                r2 = A2BYTES(cpy_s+8);
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    A2BYTES(dst+8) = r2;
                                    dst += (STEPSIZE+2);
                                }
                                break;
                              case 11 :
                                r = A8BYTES(cpy_s);
                                r2 = A2BYTES(cpy_s+8);
                                r3 = cpy_s[10]
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    A2BYTES(dst+8) = r2;
                                    dst[10] = r3;
                                    dst += (STEPSIZE+3);
                                }
                                break;
                              case 12 :
                                r = A8BYTES(cpy_s);
                                r2 = A4BYTES(cpy_s+8);
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    A2BYTES(dst+8) = r2;
                                    dst += (STEPSIZE+3);
                                }
                                break;
                              case 12 :
                                r = A8BYTES(cpy_s);
                                r2 = A4BYTES(cpy_s+8);
                                while (dst < cpy_e) {
                                    A8BYTES(dst) = r;
                                    A2BYTES(dst+8) = r2;
                                    dst += (STEPSIZE+3);
                                }
                                break;
#endif
                            }
                            dst = cpy_e; /* Correct for possible overcopy */
                        } else {

                          if (cpy_e < d_safe_end) {
                              LZJB_QUICKCOPY(dst,cpy_s,cpy_e); /* Copy by 8 */
                              dst = cpy_e; /* Correct for possible overcopy */
                          }
                          else {
                              if (cpy_e < d_safe_end)
                                  LZJB_QUICKCOPY(dst,cpy_s,d_safe_end); /* Copy by 8 */
                              LZJB_SLOWCOPY(dst,cpy_s,cpy_e); /* Copy remainder */
                          }
                        }
                    } else
                        if (dst < d_end) *dst++ = *src++; /* Copy a single literal */
                }
            }
        }

        /* Test Code to check all source is used. INCOMPATIBLE WITH ZFS API */
        if ((s_start + s_len) != src)
          return ((void*)src-(void*)s_start);

        return (0);
}
