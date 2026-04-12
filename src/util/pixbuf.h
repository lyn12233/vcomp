/** @file
 */
#ifndef C1_UTIL_PIXBUF_H
#define C1_UTIL_PIXBUF_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

#include "mem.h"

// to represent default length when calling fromview()
#define C1_PIXBUF_NONE (0xffff)
// restriction to pixel numbers, considering 2**24*12 << 2**32 and 2**12 << 2**16
#define C1_PIXBUF_LENMAX (4096)
#define C1_PIXBUF_TYPE_CNT (8)

enum {
    PIXBUF_C1I8 = 0,
    PIXBUF_C3I8,
    PIXBUF_C1I16,
    PIXBUF_C3I16,
    PIXBUF_C1I32,
    PIXBUF_C3I32,
    PIXBUF_C1F32,
    PIXBUF_C3F32,
};
typedef uint8_t PIXBUF_TYPE;

struct c1_pixbuf_s {
    uint8_t type;
    // inverse order, treat y as h-1-y e.g.
    uint8_t h_inv, w_inv;
    // range of indices x,y
    uint16_t h, w;
    // per pix, access like pix_type[y*h_stride+x*w_stride+offs]
    uint32_t h_stride, w_stride, offs;
    c1_sptr_t *buf;
};
typedef struct c1_pixbuf_s c1_pixbuf_t;

c1_pixbuf_t c1_pixbuf_create(uint8_t type, uint16_t h, uint16_t w);
// to create from sptr, assign {...,sptr} then inc sptr
c1_pixbuf_t c1_pixbuf_fromview(const c1_pixbuf_t *pix, int h_slice[3], int w_slice[3]);
void *c1_pixbuf_get(c1_pixbuf_t *pix, int h, int w);
static const void *c1_pixbuf_getc(const c1_pixbuf_t *pix, int h, int w) {
    return c1_pixbuf_get((c1_pixbuf_t *)pix, h, w);
}
static int32_t *c1_pixbuf_geti32(c1_pixbuf_t *pix, int h, int w) {
    return (int32_t *)c1_pixbuf_get(pix, h, w);
}
// clear is simply decref
int c1_pixbuf_clear(c1_pixbuf_t *pix);

void c1_pixbuf_repr(FILE *f, const c1_pixbuf_t *pix);

#ifdef __cplusplus
}
#endif
#endif