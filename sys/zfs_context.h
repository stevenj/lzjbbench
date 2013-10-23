/* Quick and Dirty Compatibility file*/
/*   allows lzjb to compile UNMODIFIED for testing with lz4 benchmarks */

#include <stdlib.h>
#include <stdint.h>

#define uchar_t uint8_t
#define NBBY                8


#define kmem_zalloc(size, flag) calloc(1,size);
#define kmem_free(buf, size)    free(buf);
