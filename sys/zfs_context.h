/* Quick and Dirty Compatibility file*/
/*   allows lzjb to compile UNMODIFIED for testing with lz4 benchmarks */

#include <stdlib.h>
#include <stdint.h>

#define uchar_t uint8_t
#define NBBY                8

#define ASSERT(x)           ((void)0)


#define __bitwise
typedef uint32_t __u32;
# define __force        __attribute__((force))
typedef __u32 __bitwise __be32;

/*
 * Little Endian or Big Endian?
 * Note: overwrite the below #define if you know your architecture endianess.
 */
#if (defined(__BIG_ENDIAN__) || defined(__BIG_ENDIAN) || \
    defined(_BIG_ENDIAN) || defined(_ARCH_PPC) || defined(__PPC__) || \
    defined(__PPC) || defined(PPC) || defined(__powerpc__) || \
    defined(__powerpc) || defined(powerpc) || \
    ((defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))))
#define __cpu_to_be32(x) ((__force __be32)__swab32((x)))
#else
#define __cpu_to_be32(x) ((__force __be32)(__u32)(x))
#endif

#define cpu_to_be32 __cpu_to_be32
#define BE_32(x)        cpu_to_be32(x)

#define BE_IN8(xa) \
        *((uint8_t *)(xa))

#define BE_IN16(xa) \
        (((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define BE_IN32(xa) \
        (((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))


#define ___constant_swab32(x) ((__u32)(                         \
         (((__u32)(x) & (__u32)0x000000ffUL) << 24) |            \
         (((__u32)(x) & (__u32)0x0000ff00UL) <<  8) |            \
         (((__u32)(x) & (__u32)0x00ff0000UL) >>  8) |            \
         (((__u32)(x) & (__u32)0xff000000UL) >> 24)))

static inline __attribute_const__ __u32 __fswab32(__u32 val)
{
 #ifdef __HAVE_BUILTIN_BSWAP32__
         return __builtin_bswap32(val);
 #elif defined(__arch_swab32)
         return __arch_swab32(val);
 #else
         return ___constant_swab32(val);
 #endif
}


/**
 * __swab32 - return a byteswapped 32-bit value
 * @x: value to byteswap
 */
#define __swab32(x)                             \
        (__builtin_constant_p((__u32)(x)) ?     \
        ___constant_swab32(x) :                 \
        __fswab32(x))

#define KM_PUSHPAGE 0

typedef void* spl_kmem_ctor_t;
typedef void* spl_kmem_dtor_t;
typedef void* spl_kmem_reclaim_t;

typedef struct spl_kmem_cache {
        size_t              cache_size;
} spl_kmem_cache_t;
#define kmem_cache_t            spl_kmem_cache_t

static inline spl_kmem_cache_t *spl_kmem_cache_create(char *name __attribute__((__unused__)), size_t size,
        size_t align __attribute__((__unused__)), spl_kmem_ctor_t ctor __attribute__((__unused__)),
        spl_kmem_dtor_t dtor __attribute__((__unused__)),
        spl_kmem_reclaim_t reclaim __attribute__((__unused__)),
        void *priv __attribute__((__unused__)), void *vmp __attribute__((__unused__)),
        int flags __attribute__((__unused__)))
{
    kmem_cache_t* c = malloc(sizeof(kmem_cache_t));
    if (c != NULL)
      c->cache_size = size;
    return c;
}

static inline void *spl_kmem_cache_alloc(spl_kmem_cache_t *skc, int flags __attribute__((__unused__)))
{
    if (skc != NULL)
        if (skc->cache_size > 0)
            return(malloc(skc->cache_size));
    return NULL;
}

static inline void spl_kmem_cache_free(spl_kmem_cache_t *skc __attribute__((__unused__)), void *obj)
{
    free(obj);
}

static inline void spl_kmem_cache_destroy(spl_kmem_cache_t *skc)
{
    free(skc);
}

#define kmem_zalloc(size, flag) calloc(1,size);
#define kmem_free(buf, size)    free(buf);

#define kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags) \
        spl_kmem_cache_create(name,size,align,ctor,dtor,rclm,priv,vmp,flags)
#define kmem_cache_alloc(skc, flags)    spl_kmem_cache_alloc(skc, flags)
#define kmem_cache_free(skc, obj)       spl_kmem_cache_free(skc, obj)
#define kmem_cache_destroy(skc)         spl_kmem_cache_destroy(skc)
