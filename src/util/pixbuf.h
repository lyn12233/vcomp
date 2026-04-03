/** @file
 */
#ifndef C1_UTIL_PIXBUF_H
#define C1_UTIL_PIXBUF_H

#include <stdint.h>

#include "mem.h"

#define C1_PIXBUF_NONE (0xffff)
#define C1_PIXBUF_LENMAX (4096)

enum {
    PIXBUF_C1I8 = 0,
    PIXBUF_C3I8,
    PIXBUF_C1I16,
    PIXBUF_C3I16,
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
// clear is simply decref
int c1_pixbuf_clear(c1_pixbuf_t *pix);

#endif