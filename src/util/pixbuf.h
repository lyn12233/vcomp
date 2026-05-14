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
    C1_PIXBUF_C1I8 = 0,
    C1_PIXBUF_C3I8,
    C1_PIXBUF_C1I16,
    C1_PIXBUF_C3I16,
    C1_PIXBUF_C1I32,
    C1_PIXBUF_C3I32,
    C1_PIXBUF_C1F32,
    C1_PIXBUF_C3F32,
};
typedef uint8_t C1_PIXBUF_TYPE;

struct c1_pixbuf_s {
    uint8_t type;
    // inverse order, treat y as h-1-y e.g.
    uint8_t h_inv, w_inv;
    // range of indices x,y
    uint16_t h, w;
    // per pix, access like (pix_type*)[y*h_stride+x*w_stride+offs]
    uint32_t h_stride, w_stride, offs;
    c1_sptr_t *buf;
};
typedef struct c1_pixbuf_s c1_pixbuf_t;

// --- ctor, converter and slicing ---

c1_pixbuf_t c1_pixbuf_create(C1_PIXBUF_TYPE type, uint16_t h, uint16_t w);
c1_pixbuf_t c1_pixbuf_from_ptr(C1_PIXBUF_TYPE type, uint16_t h, uint16_t w, const void *p);

// to create from sptr, assign {...,sptr} then inc sptr
// "view" on existing pixbuf

c1_pixbuf_t c1_pixbuf_fromview(const c1_pixbuf_t *pix, int h_slice[3], int w_slice[3]);
c1_pixbuf_t c1_pixbuf_cvt(const c1_pixbuf_t *in, C1_PIXBUF_TYPE type);
c1_pixbuf_t c1_pixbuf_cvt_rgbi8_to_yuv16(const c1_pixbuf_t *in);
c1_pixbuf_t c1_pixbuf_cvt_yuv16_to_rgbi8(const c1_pixbuf_t *in);
int c1_pixbuf_paste(c1_pixbuf_t *trg, const c1_pixbuf_t *src, int y, int x);
c1_pixbuf_t c1_pixbuf_dupview(const c1_pixbuf_t *pix);
c1_pixbuf_t c1_pixbuf_fromchnl(const c1_pixbuf_t *pix, int chnl);

// --- data access ---

void *c1_pixbuf_get(c1_pixbuf_t *pix, int h, int w);
static const void *c1_pixbuf_getc(const c1_pixbuf_t *pix, int h, int w) {
    return c1_pixbuf_get((c1_pixbuf_t *)pix, h, w);
}
static int32_t *c1_pixbuf_geti32(c1_pixbuf_t *pix, int h, int w) {
    return (int32_t *)c1_pixbuf_get(pix, h, w);
}
static const int32_t *c1_pixbuf_geti32c(const c1_pixbuf_t *pix, int h, int w) {
    return (const int32_t *)c1_pixbuf_getc(pix, h, w);
}
static int16_t *c1_pixbuf_geti16(c1_pixbuf_t *pix, int h, int w) {
    return (int16_t *)c1_pixbuf_get(pix, h, w);
}
static const int16_t *c1_pixbuf_geti16c(const c1_pixbuf_t *pix, int h, int w) {
    return (const int16_t *)c1_pixbuf_getc(pix, h, w);
}

// --- manipulation ---

// --- dtor ---
// clear is simply decref
int c1_pixbuf_clear(c1_pixbuf_t *pix);

// --- repr ---
void c1_pixbuf_repr(FILE *f, const c1_pixbuf_t *pix);

// --- helper ---

// Color map for segmentation annotation (32 classes, RGB values)
// Each row represents one class with RGB values (0-255)
static const uint8_t c1_color_map[32][3] = {
    //
    {0, 0, 0},       /* Class 1 (red)*/ {255, 0, 0},              /* Class 2 (green)*/
    {0, 255, 0},     /* Class 3 (blue)*/ {0, 0, 255},             /* Class 4 (yellow)*/
    {255, 255, 0},   /* Class 5 (cyan)*/ {0, 255, 255},           /* Class 6 (magenta)*/
    {255, 0, 255},   /* Class 7 (orange)*/ {255, 128, 0},         /* Class 8 (purple)*/
    {128, 0, 255},   /* Class 9 (lime)*/ {128, 255, 0},           /* Class 10 (teal)*/
    {0, 128, 128},   /* Class 11 (pink)*/ {255, 192, 203},        /* Class 12 (brown)*/
    {139, 69, 19},   /* Class 13 (navy)*/ {0, 0, 128},            /* Class 14 (olive)*/
    {128, 128, 0},   /* Class 15 (maroon)*/ {128, 0, 0},          /* Class 16 (forest */
    {34, 139, 34},   /* Class 17 (royal */ {65, 105, 225},        /* Class 18 (gold)*/
    {255, 215, 0},   /* Class 19 (coral)*/ {255, 127, 80},        /* Class 20 (indigo)*/
    {75, 0, 130},    /* Class 21 (khaki)*/ {240, 230, 140},       /* Class 22 (lavender)*/
    {230, 230, 250}, /* Class 23 (salmon)*/ {250, 128, 114},      /* Class 24 (thistle)*/
    {216, 191, 216}, /* Class 25 (tomato)*/ {255, 99, 71},        /* Class 26 (turquoise)*/
    {64, 224, 208},  /* Class 27 (violet)*/ {238, 130, 238},      /* Class 28 (wheat)*/
    {245, 222, 179}, /* Class 29 (yellow-green)*/ {154, 205, 50}, /* Class 30 (steel blue)*/
    {70, 130, 180},  /* Class 31 (dark orchid)*/ {153, 50, 204}   //
};

#ifdef __cplusplus
}
#endif
#endif