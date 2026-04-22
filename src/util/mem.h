/** @file
 */
#ifndef C1_UTIL_MEM_H
#define C1_UTIL_MEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// memory pool
struct c1_mpool_s {
    const uint16_t sz; // elem_sz
    const uint8_t nb;
    void *root_;
};
typedef struct c1_mpool_s c1_mpool_t;
// to init an uninitialized mpool, directly assign {sz, nb, NULL}
// a static mpool is like c1_mpool_t xxx = {sz, nb, NULL}
// alloc a buffer of size sz using p
void *c1_mpool_alloc(c1_mpool_t *p);
// free a buffer in the pool
int c1_mpool_dealloc(c1_mpool_t *p, void *buf);
int c1_mpool_dbgcnt(const c1_mpool_t *p);

// default mpools with size 1, .., 256 (bytes)
extern c1_mpool_t c1_mpool_defs[9];
// fast default alloc, suppose size is 2**n and within range, no checking,
// but bigger 2**n downgrades to malloc.
static void *c1_mpool_alloc_def(uint32_t size) {
    if (size > 256)
        return malloc(size);
    int i = 0;
    while (size >>= 1)
        i++;
    return c1_mpool_alloc(c1_mpool_defs + i);
}
// fast default dealloc.
static int c1_mpool_dealloc_def(uint32_t size, void *buf) {
    if (size > 256) {
        free(buf);
        return 0;
    }
    int i = 0;
    while (size >>= 1)
        i++;
    return c1_mpool_dealloc(c1_mpool_defs + i, buf);
}

// shared_ptr
struct c1_sptr_s {
    void *ptr;
    void (*dtor_)(void *ptr);
    uint32_t cnt_;
};
typedef struct c1_sptr_s c1_sptr_t;
// to create it, first create ptr with relevant ctor, then create sptr
c1_sptr_t *c1_sptr_create(void *ptr, void (*dtor)(void *));
int c1_sptr_incref(c1_sptr_t **p);
int c1_sptr_decref(c1_sptr_t **p);

void c1_dump_buf(void *buf, uint32_t sz);
void c1_dump_buf_f(FILE *fp, void *buf, uint32_t sz);

#ifdef __cplusplus
}
#endif
#endif // once