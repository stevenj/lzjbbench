/*
 * Improved LZJB - Fast LZJB decompression algorithm
 * Copyright (C) 2013, Steven Johnson.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - Improved LZJB source repository : https://github.com/stevenj/lzjbbench
 */
#include <sys/zfs_context.h>
#include <sys/sysmacros.h>

/*
 * This is the LZJB bitstream and basic decompression procedure:
 *
 * <copymap [8 bit] > (from src)
 * Starting from LSbit of copymap.
 *   bit = 1?
 *     < 16 bit copylength:offset > ( MSByte first ) (from src)
 *     copy length = 6 most significant bits + 3.
 *     offset = 10 least signicant bits.
 *     Copy copylength bytes from dest - offset to dest.
 *   bit = 0?
 *     output a literal byte. (from src to dest)
 *
 * once byte exhausted, repeat.
 */

/* This is completely re-written.  No code from standard lzjb is used.
 * Some variable names are preserved to ease code review.
 * Borrows some techniques or speed ideas from LZ4.
 *
 * Uses Modified versions of LZ4 defines and macros.
 */

/*
 * CPU Feature Detection
 */

/* 32 or 64 bits ? */
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || \
    defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || \
    defined(__LP64__) || defined(_LP64))
#define LZJB_ARCH64 1
#else
#define LZJB_ARCH64 0
#warning LZJB - 32 BIT IS UNTESTED!!!!
#endif

/*
 * Little Endian or Big Endian?
 * Note: overwrite the below #define if you know your architecture endianess.
 */
#if (defined(__BIG_ENDIAN__) || defined(__BIG_ENDIAN) || \
    defined(_BIG_ENDIAN) || defined(_ARCH_PPC) || defined(__PPC__) || \
    defined(__PPC) || defined(PPC) || defined(__powerpc__) || \
    defined(__powerpc) || defined(powerpc) || \
    ((defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))))
#define LZJB_BIG_ENDIAN 1
#warning LZJB - BIG ENDIAN IS UNTESTED!!!!
#else
/*
 * Little Endian assumed. PDP Endian and other very rare endian format
 * are unsupported.
 */
#endif

/*
 * Unaligned memory access is automatically enabled for "common" CPU,
 * such as x86. For others CPU, the compiler will be more cautious, and
 * insert extra code to ensure aligned access is respected. If you know
 * your target CPU supports unaligned memory access, you may want to
 * force this option manually to improve performance
 */
#if defined(__ARM_FEATURE_UNALIGNED)
#define LZJB_FORCE_UNALIGNED_ACCESS 1
#endif

/*
 * Illumos : we can't use GCC's __builtin_ctz family of builtins in the
 * kernel
 * Linux : we can use GCC's __builtin_ctz family of builtins in the
 * kernel
 */
#undef  LZJB_FORCE_SW_BITCOUNT

/*
 * Linux : GCC_VERSION is defined as of 3.9-rc1, so undefine it.
 * torvalds/linux@3f3f8d2f48acfd8ed3b8e6b7377935da57b27b16
 */
#ifdef GCC_VERSION
#undef GCC_VERSION
#endif

#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

/*
#if (GCC_VERSION >= 302) || (__INTEL_COMPILER >= 800) || defined(__clang__)
#define expect(expr, value)    (__builtin_expect((expr), (value)))
#else
#define expect(expr, value)    (expr)
#endif

#ifndef likely
#define likely(expr)    expect((expr) != 0, 1)
#endif

#ifndef unlikely
#define unlikely(expr)  expect((expr) != 0, 0)
#endif

#define lz4_bswap16(x) ((unsigned short int) ((((x) >> 8) & 0xffu) | \
    (((x) & 0xffu) << 8)))
*/

#ifndef LZJB_FORCE_UNALIGNED_ACCESS
#pragma pack(1)
#endif

typedef struct _U16_S {
    uint16_t v;
} U16_S;
typedef struct _U32_S {
    uint32_t v;
} U32_S;
typedef struct _U64_S {
    uint64_t v;
} U64_S;

#ifndef LZJB_FORCE_UNALIGNED_ACCESS
#pragma pack()
#endif

#define A64(x) (((U64_S *)(x))->v)
#define A32(x) (((U32_S *)(x))->v)
#define A16(x) (((U16_S *)(x))->v)

#if LZJB_ARCH64
#define LZJB_STEPSIZE          8
#define LZJB_ONESTEP(s,d)      A64(d) = A64(s)
#define LZJB_COPYSTEP(s, d)    LZJB_ONESTEP(s,d); d += 8; s += 8;
#define LZJB_COPY8(s, d)       LZJB_COPYSTEP(s, d)
#define LZJB_COPY4(s, d)       A32(d) = A32(s); d += 4; s += 4;
#else /* !LZ4_ARCH64 */
#define LZJB_STEPSIZE          4
#define LZJB_ONESTEP(s,d)      A32(d) = A32(s)
#define LZJB_COPYSTEP(s, d)    LZJB_ONESTEP(s,d); d += 4; s += 4;
#define LZJB_COPY8(s, d)       LZJB_COPYSTEP(s, d); LZJB_COPYSTEP(s, d)
#define LZJB_COPY4(s, d)       LZJB_COPYSTEP(s, d)
#endif

#define LZJB_QUICKCOPY(s, d, e) do { LZJB_COPY8(s, d) } while (d < e);

/*
#define LZJB_ARCH64               LZ4_ARCH64
#define LZJB_STEPSIZE             STEPSIZE
#define LZJB_COPY8(s, d)          LZ4_COPYPACKET(s, d)
#define LZJB_COPY4(s, d)          A32(d) = A32(s); d += 4; s += 4;
#define LZJB_QUICKCOPY(s, d, e)   LZ4_WILDCOPY(s, d, e)
*/
/*
#if defined(LZ4_BIG_ENDIAN)
#define LZJB_BIG_ENDIAN           LZ4_BIG_ENDIAN
#endif
*/

#define LZJB_MATCH_BITS           (6)
#define LZJB_OFFSET_BITS          (10)
#define LZJB_MATCH_MIN            (3)
#define LZJB_OFFSET_MASK          ((1<<LZJB_OFFSET_BITS)-1)

#ifdef KERN_DEOPT
__attribute__ ((__target__ ("no-mmx,no-sse,no-sse2")))
#endif
static inline void LZJB_RLE_DECOMPRESS(uint16_t offset, uchar_t *cpy_s, uchar_t *dst, uchar_t *cpy_e) {
    /*
     * Will copy a run of bytes, immediately preceding the dst point.
     * dst and src are not modified on return so need to be adjusted.
     * by the caller.
     *
     * It has been extracted from the main algorithm to aid readability.
     * It should be inlined by the compiler, so this should have no
     * impact on the codes performance.
     */
#if LZJB_ARCH64
    uint64_t run1;
    uint32_t run2;
    uint16_t run3;
#else
    uint32_t run1;
    uint32_t run2;
#endif

    switch(offset) {
        case 1 :
            run1 = *cpy_s;
            run1 |= (run1 << 8);
            run1 |= (run1 << 16);
#if LZJB_ARCH64
            run1 |= (run1 << 32);
#endif
            do {
#if LZJB_ARCH64
                A64(dst) = run1;
#else
                A32(dst) = run1;
#endif
                dst += LZJB_STEPSIZE;
            } while (dst < cpy_e);
            break;
        case 2 :
            run1 = A16(cpy_s);
            run1 |= (run1 << 16);
#if LZJB_ARCH64
            run1 |= (run1 << 32);
#endif
            do {
#if LZJB_ARCH64
                A64(dst) = run1;
#else
                A32(dst) = run1;
#endif
                dst += LZJB_STEPSIZE;
            } while (dst < cpy_e);
            break;
        case 3 :
            /* 6 Bytes = 32bits + 16 bits - endian sensitive */
            /* TODO: Test if faster to extend to 12 byte copy on X86-64 */
#if defined(LZ4_BIG_ENDIAN)
            /*
             * Ad:   0   1  2   3     4  5
             * Rd: R1a R1b R1c  0
             * Wr:[R1a:R1b:R1c:R1a] [R1b:Rc]
             */
            run1 = A32(cpy_s) & 0xFFFFFF00;
            run2 = (run1 >> 8) & 0xFFFF;
            run1 = run1 | (run1 >> 24);
#else
            /*
             * Ad:    0   1   2   3     4  5
             * Rd:  R1d R1c  R1b  0
             * Ad:    3   2   1   0     5   4
             * Wr: [R1d:R1b:R1c:R1d] [R1b:R1c]
             */
            run1  = A32(cpy_s) & 0x00FFFFFF;
            run2  = (run1 >> 8) & 0xFFFF;
            run1  = run1 | (run1 << 24);
#endif
            do {
                A32(dst) = run1;
                dst+=4;
                if (dst < cpy_e) {
                    A16(dst) = run2;
                    dst += 2;
                }
            } while (dst < cpy_e);
            break;

        case 4 :
            run1 = A32(cpy_s);
#if LZJB_ARCH64
            run1 = run1 | run1 << 32;
#endif
            do {
#if LZJB_ARCH64
                A64(dst) = run1;
#else
                A32(dst) = run1;
#endif
                dst += LZJB_STEPSIZE;
            } while (dst < cpy_e);
        break;
#if LZJB_ARCH64
        case 5 :
            /* 5 Bytes, can copy as 10 - 1 x 64 bits + 1 x 16 bits */
            /* Endian sensitive copy */
#if defined(LZ4_BIG_ENDIAN)
            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9
             * Rd: R1h, R1g, R1f, R1e, R1d,   0    0    0      0    0
             * Wr:[R1h, R1g, R1f, R1e, R1d, R1h, R1g, R1f], [R1e, R1d]
             */
            run1 = A64(cpy_s) & 0xFFFFFFFFFF000000;
            run2 = (run1 >> 24);
            run1 |= ((run1 >> 40) & 0xFFFFFF);
#else
            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9
             * Rd: R1a, R1b, R1c, R1d, R1e,   0    0    0      0    0
             * Ad:   7    6    5    4    3    2    1    0      9    8
             * Wr:[R1c, R1b, R1a, R1e, R1d, R1c, R1b, R1a], [R1e, R1d]
             */
            run1 = A64(cpy_s) & 0x000000FFFFFFFFFF;
            run2  = (run1 >> 24);
            run1 |= (run1 << 40);
#endif
            do {
                A64(dst) = run1;
                if (dst < cpy_e) {
                    A16(dst+8) = run2;
                }
                dst += 10;
            } while (dst < cpy_e);
        break;

        case 6 :
            /* 6 Bytes, can copy as 12 [8] + [4] */
            /* Endian sensitive copy */
#if defined(LZ4_BIG_ENDIAN)

            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9   10  11
             * Rd: R1h, R1g, R1f, R1e, R1d, R1c,   0    0      0    0    0   0
             * Wr:[R1h, R1g, R1f, R1e, R1d, R1c, R1h, R1g], [R1f, R1e, R1d, R1c]
             */
            run1  = A64(cpy_s) & 0xFFFFFFFFFFFF0000;
            run2  = (run1 >> 16);
            run1 |= ((run1 >> 48) & 0xFFFF);
#else
            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9   10   11
             * Rd: R1a, R1b, R1c, R1d, R1e, R1f,   0    0      0    0    0    0
             * Ad:   7    6    5    4    3    2    1    0     11   10    9    8
             * Wr:[R1b, R1a, R1f, R1e, R1d, R1c, R1b, R1a], [R1f, R1e, R1d, R1c]
             */
            run1  = A64(cpy_s) & 0x0000FFFFFFFFFFFF;
            run2  = (run1 >> 16);
            run1 |= (run1 << 48);
#endif
            do {
                A64(dst) = run1;
                if (dst < cpy_e) {
                    A32(dst+8) = run2;
                }
                dst += 12;
            } while (dst < cpy_e);
        break;
        case 7 :
            /* 7 Bytes, can copy as 14 [8] + [4] + [2] */
            /* Endian sensitive copy */
#if defined(LZ4_BIG_ENDIAN)

            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9   10  11      12   13
             * Rd: R1h, R1g, R1f, R1e, R1d, R1c, R1b    0      0    0    0   0  ,    0    0
             * Wr:[R1h, R1g, R1f, R1e, R1d, R1c, R1b, R1h], [R1g, R1f, R1e, R1d], [R1c, R1b]
             */
            run1  = A64(cpy_s) & 0xFFFFFFFFFFFFFF00;
            run2  = (run1 >> 24);
            run3  = (run1 >> 8);
            run1 |= ((run1 >> 56) & 0xFF);
#else
            /*
             * Ad:   0    1    2    3    4    5    6    7      8    9   10  11      12   14
             * Rd: R1a, R1b, R1c, R1d, R1e, R1f, R1g    0      0    0    0   0  ,    0    0
             * Ad:   7    6    5    4    3    2    1    0     11   10    9    8     13   12
             * Wr:[R1a, R1g, R1f, R1e, R1d, R1c, R1b, R1a], [R1e, R1d, R1c, R1b], [R1g, R1f]
             */
            run1  = A64(cpy_s) & 0x00FFFFFFFFFFFFFF;
            run2  = (run1 >> 8);
            run3  = (run1 >> 40);
            run1 |= (run1 << 56);
#endif
            do {
                A64(dst) = run1;
                if (dst < cpy_e) {
                    A32(dst+8) = run2;
                }
                if (dst < cpy_e) {
                    A16(dst+12) = run3;
                }
                dst += 14;
            } while (dst < cpy_e);
        break;
        case 8 :
            run1 = A64(cpy_s);
            do {
                A64(dst) = run1;
                dst += 8;
            } while (dst < cpy_e);
        break;
#endif /* STEPSIZE == 8 */
    }
}

#ifdef KERN_DEOPT
__attribute__ ((__target__ ("no-mmx,no-sse,no-sse2")))
#endif
int
lzjb_decompress_fast(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
    /*
     * s_start = start of compressed data buffer.
     * d_start = start of area to place decompressed data.
     * s_len   = length of the compressed data buffer.
     * d_len   = the expected length of the decompressed data.
     * n       = in ZFS is compression factor.
     *           for lzjb decompression speed optimization it CAN
     *           specify the MAXIMUM safe size of the decompression
     *           buffer.  Which, if it is at least 8 bytes larger than
     *           d_len will allow decompression to run at maximum speed
     *           without worrying about buffer over run.
     *           IF n is less than d_len it will not apply.
     */
    uchar_t *src   = s_start; /* Current pos in src buffer */
    uchar_t *dst   = d_start; /* Current pos in dst buffer */
    uchar_t *d_end = dst + d_len; /* End of decompression marker */
    uchar_t *s_end = d_end - LZJB_STEPSIZE;
                              /* The safe end of decompression, after
                               * here we need to make sure no buffer
                               * over runs occur.
                               */
    uchar_t copymap  = 0; /* The current map of Literals or Copy Runs */
    uchar_t copyleft = 0; /* The number of unconsumed bits in copymap */
    uint16_t run;         /* The size of ANY Run, literal or copy */
    uint16_t offset;      /* Offset into the dst buffer to get copy from */
    uchar_t *cpy_s;       /* The start of any redundant data copy */
    uchar_t *cpy_e;       /* The end of any redundant data copy */

    /* Use the n parameter to get the true safe size of the
     * decompression buffer.
     *
     * Because a copymap can represent 8 literals, the maximum overrun
     * is 8 bytes.  This algorithm will perform better on 64 bit
     * architectures.
     */
    if (n > (int)d_len) {
#if LZJB_ARCH64
        s_end = MIN(d_end, (dst + n - LZJB_STEPSIZE));
#else
        s_end = MIN(d_end, (dst + n - (LZJB_STEPSIZE*2)));
#endif
    }

    /* The size of the destination buffer controls decompression.
     * The last copymap may not all be used, decompression stops
     * when the end of the destination buffer is reached.
     * We use the safe end as the end here, and fix up the last
     * possible bytes between the d_end and s_end later.
     * It is, by definition, safe to over run the "safe end", by
     * at most, LZJB_STEPSIZE bytes.
     */
    while (dst < s_end) {

        /* read the copy map from the src buffer */
        copymap  = *src++;

        /* This is generalised at the end, does removing it reduce
         * performance ??
         * Yes it does.  Benchmarking showed an around 6% drop
         * if we remove this and rely on the generalized code.
         */
        if (copymap == 0x00) {
            /* Special Case #1
             * copymap of zero is 8 literals, handle that.
             * This special case makes handling "uncompressable"
             * compressed streams dramatically faster.
             */
            LZJB_COPY8(src,dst); /* Copy 8 bytes & update src and dst */
        } else {
            copyleft = 8; /* Number of bits to process in copymap */
            run=0;        /* No runs yet counted */
            while (copymap > 0) {

#if defined(__GNUC__) && (GCC_VERSION >= 304) && \
    !defined(LZ4_FORCE_SW_BITCOUNT)
                /* Using a ctz (bsf instruction on x86. Speeds literal
                 * copying significantly.  If it is usable, then use
                 * it.
                 */
                run = __builtin_ctz(copymap);
                copymap >>= run;
                copyleft -= run;
                { /* This bracket closes the main copy bracket
                   * and allows us to use an if in the alternate
                   * software only version below.
                   */
#else
                /* Otherwise scan each literal out manually */
                if ((copymap & 1) == 0) {
                    run++;
                } else {
#endif
                    /*
                     * run will only by > 0 if there are literals to
                     * copy, so do that here. Before we process the next
                     * redundant copy run.
                     */
                    if (run > 0) {
                        LZJB_ONESTEP(src,dst);
#if LZJB_ARCH64 == 0
                        if (copyleft > LZJB_STEPSIZE) {
                            LZJB_ONESTEP(src+LZJB_STEPSIZE,dst+LZJB_STEPSIZE);
                        }
#endif
                        src += run;
                        dst += run;
                    }

                    /*
                     * Get the offset and run length of the region to copy
                     */
                    offset = BE_IN16(src);
                    run = (offset >> LZJB_OFFSET_BITS) + LZJB_MATCH_MIN;
                    offset &= LZJB_OFFSET_MASK;
                    src+=2;

                    /* calculate the source to copy from, and make sure its in range */
                    cpy_s = dst - offset;
                    /* we will be copying up to here */
                    cpy_e = dst + run;

                    /* Sanity Check - Can not copy from before dst
                     * buffer, or after it.
                     */
                    if (cpy_s < (uchar_t *)d_start) return (-1);
                    /*
                     * This check should always be true for a valid
                     * bit stream.  It also means the worst we can ever
                     * over copy is "LZJB_STEPSIZE-1".
                     */
                    if (cpy_e > (uchar_t *)d_end)   return (-2);

                    /* Special Case #3.
                     * IF the region to copy is very close to the
                     * current destination, it becomes an effective
                     * multi-byte run length encoded stream.
                     * by detecting this we prevent multiple reads
                     * from being required, saving time, and improving
                     * memory efficiency.
                     *
                     * This is also required by the over-copying
                     * optimization.  As over-copying will produce
                     * incorrect data at offsets less than STEP SIZE.
                     *
                     * However, we only do this id the run length is
                     * greater than the offset, otherwise a better
                     * optimization follows.
                     */
                    if ((offset <= LZJB_STEPSIZE) && (offset < run)) {
                        LZJB_RLE_DECOMPRESS(offset, cpy_s, dst, cpy_e);
                        dst = cpy_e;
                    } else if (run <= LZJB_STEPSIZE) {
                        /* short run, can be copied without looping */
                        LZJB_ONESTEP(cpy_s,dst);
                        dst = cpy_e;
                    } else if (run <= LZJB_STEPSIZE*2) {
                        /* short run, can be copied without looping */
                        LZJB_ONESTEP(cpy_s,dst);
                        LZJB_ONESTEP(cpy_s+LZJB_STEPSIZE,dst+LZJB_STEPSIZE);
                        dst = cpy_e;
                    } else if (run <= LZJB_STEPSIZE*3) {
                        /* short run, can be copied without looping
                         *
                         * Benchmarking showed returns for unrolling
                         * short runs up to here, but after here they
                         * diminished rapidly.
                         */
                        LZJB_ONESTEP(cpy_s,dst);
                        LZJB_ONESTEP(cpy_s+LZJB_STEPSIZE,dst+LZJB_STEPSIZE);
                        LZJB_ONESTEP(cpy_s+(LZJB_STEPSIZE*2),dst+(LZJB_STEPSIZE*2));
                        dst = cpy_e;
                    } else {
                        LZJB_QUICKCOPY(cpy_s, dst, cpy_e);
                        dst = cpy_e; /* Correct for possible over copy */
                    }
                    run=0; /* Finished with run, so clear it */

                }
                copymap >>= 1;
                copyleft--;
            }

            /* Special Case #2
             * copymap becomes zero.
             * All the rest of copymap is literals. so just copy them.
             * Using an inline protects src and dst from being changed.
             * Allows to to more easily update them with the correct
             * step.
             */
            if (copyleft > 0) {
                LZJB_ONESTEP(src,dst);
#if LZJB_ARCH64 == 0
                if (copyleft > LZJB_STEPSIZE) {
                    LZJB_ONESTEP(src+LZJB_STEPSIZE,dst+LZJB_STEPSIZE);
                }
#endif
                src += copyleft;
                dst += copyleft;
            }

        }

    }

    /* Fix up last bytes, being careful not to over run the dst buffer. */
    if (dst < d_end) {
       /* Read the LAST copymap from the src buffer.
        * This copymap may not be fully used.
        * Because the safe_end is >= 8 bytes from the true dst end
        * One copy map can easily represent 8 literals which is the
        * worst case scenario.  Accordingly there will be no more.
        */
       copymap = *src++;
       do {
           if ((copymap & 1) == 0) {
             *dst++ = *src++;
           } else {
             offset = BE_IN16(src);
             run = (offset >> LZJB_OFFSET_BITS) + LZJB_MATCH_MIN;
             offset &= LZJB_OFFSET_MASK;
             src+=2;

             cpy_s = dst - offset;
             cpy_e = dst + run;

             /* Sanity Check - Can not copy from before dst
              * buffer, or after it.
              */
             if (cpy_s < (uchar_t *)d_start) return (-1);
             if (cpy_e > (uchar_t *)d_end)   return (-2);

             while (dst < cpy_e) {
                 *dst++ = *cpy_s++;
             }
           }
           copymap >>= 1;
       } while (dst < d_end);
    }

    return (0);
}
