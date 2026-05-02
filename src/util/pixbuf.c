#include "pixbuf.h"
#include "log.h"
#include "mem.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t c1pb__sz(C1_PIXBUF_TYPE t) {
    static const uint8_t map[C1_PIXBUF_TYPE_CNT] = {1, 3, 2, 6, 4, 12, 4, 12};
    return map[t];
}
static const char *c1pb__type2str(C1_PIXBUF_TYPE t) {
    static const char *map[C1_PIXBUF_TYPE_CNT] = {
        "C1I8", "C3I8", "C1I16", "C3I16", "C1I32", "C3I32", "C1F32", "C3F32",
    };
    return map[t];
}
static const char *c1pb__bool2str(uint8_t b) {
    return b ? "true" : "false";
}

// stt,end,step to stt,nb,step
static void c1pb__norm_slice(uint16_t sz, int s[3]) {
    s[0] = s[0] < 0 ? s[0] + sz : s[0], s[1] = s[1] < 0 ? s[1] + sz : s[1];
    assert_fatal(s[0] == C1_PIXBUF_NONE || s[0] >= 0 && s[0] <= sz);
    assert_fatal(s[1] == C1_PIXBUF_NONE || s[1] >= 0 && s[1] <= sz);
    assert_fatal(s[2] != 0);
    s[0] = s[0] == C1_PIXBUF_NONE ? (s[2] < 0 ? sz - 1 : 0) : s[0];
    s[1] = s[1] == C1_PIXBUF_NONE ? (s[2] < 0 ? -1 : sz) : s[1];
    if (s[2] < 0) {
        int temp = s[0];
        s[0] = s[1] + 1;
        s[1] = temp + 1;
        s[2] = -s[2];
    }
    s[1] = (s[1] - s[0]) / s[2];
    s[1] = s[1] < 0 ? 0 : s[1];
}

c1_pixbuf_t c1_pixbuf_create(C1_PIXBUF_TYPE type, uint16_t h, uint16_t w) {
    assert_fatal(h < C1_PIXBUF_LENMAX && w < C1_PIXBUF_LENMAX);
    void *data = malloc(h * w * c1pb__sz(type));
    assert_fatal(data);
    memset(data, 0, h * w * c1pb__sz(type));
    c1_sptr_t *p = c1_sptr_create(data, free);
    return (c1_pixbuf_t){type, 0, 0, h, w, w, 1, 0, p};
}
c1_pixbuf_t c1_pixbuf_from_ptr(C1_PIXBUF_TYPE type, uint16_t h, uint16_t w, const void *p) {
    c1_pixbuf_t res = c1_pixbuf_create(type, h, w);
    memcpy(res.buf->ptr, p, h * w * c1pb__sz(type));
    return res;
}
c1_pixbuf_t c1_pixbuf_fromview(const c1_pixbuf_t *pix, int h_slice[3], int w_slice[3]) {
    // check inverse, normalize and check h_slice
    uint8_t h_inv = h_slice[2] < 0, w_inv = w_slice[2] < 0;
    c1pb__norm_slice(pix->h, h_slice), c1pb__norm_slice(pix->w, w_slice);
    // gen new pixbuf
    c1_pixbuf_t res = {
        pix->type,
        (uint8_t)(pix->h_inv ^ h_inv),
        (uint8_t)(pix->w_inv ^ w_inv),
        (uint16_t)h_slice[1],
        (uint16_t)w_slice[1],
        pix->h_stride * h_slice[2],
        pix->w_stride * w_slice[2],
        pix->offs + pix->h_stride * h_slice[0] + pix->w_stride * w_slice[0],
        pix->buf,
    };
    if (res.buf)
        c1_sptr_incref(&res.buf);
    return res;
}
c1_pixbuf_t c1_pixbuf_cvt(const c1_pixbuf_t *in, C1_PIXBUF_TYPE type) {
    c1_pixbuf_t res = c1_pixbuf_create(type, in->h, in->w);
    if (in->type == type)
        return res;
    if (in->type == C1_PIXBUF_C1I32) {
        if (type == C1_PIXBUF_C1I16) {
            for (int y = 0; y < in->h; y++) {
                for (int x = 0; x < in->w; x++) {
                    *c1_pixbuf_geti16(&res, y, x) = (int16_t)*c1_pixbuf_geti32c(in, y, x);
                }
            }
        } else {
            goto error;
        }
    } else if (in->type == C1_PIXBUF_C1I16) {
        if (type == C1_PIXBUF_C1I32) {
            for (int y = 0; y < in->h; y++) {
                for (int x = 0; x < in->w; x++) {
                    *c1_pixbuf_geti32(&res, y, x) = *c1_pixbuf_geti16c(in, y, x);
                }
            }
        }
    } else if (in->type == C1_PIXBUF_C3I8) {
        if (type == C1_PIXBUF_C3I16) {
            for (int y = 0; y < in->h; y++) {
                for (int x = 0; x < in->w; x++) {
                    const uint8_t *p_in = c1_pixbuf_getc(in, y, x);
                    int16_t *p_out = c1_pixbuf_get(&res, y, x);
                    for (int ci = 0; ci < 3; ci++) {
                        p_out[ci] = p_in[ci];
                    }
                }
            }
        }
    } else if (in->type == C1_PIXBUF_C3I16) {
        if (type == C1_PIXBUF_C3I8) {
            for (int y = 0; y < in->h; y++) {
                for (int x = 0; x < in->w; x++) {
                    const int16_t *p_in = c1_pixbuf_getc(in, y, x);
                    uint8_t *p_out = c1_pixbuf_get(&res, y, x);
                    for (int ci = 0; ci < 3; ci++) {
                        p_out[ci] = (uint8_t)(p_in[ci] < 0 ? 0 : p_in[ci] > 255 ? 255 : p_in[ci]);
                    }
                }
            }
        }
    } else {
        goto error;
    }
    return res;
error:
    warning("can not convert pixbuf type from %s to %s", c1pb__type2str(in->type), c1pb__type2str(type));
    c1_pixbuf_clear(&res);
    return (c1_pixbuf_t){0};
}

int c1_pixbuf_paste(c1_pixbuf_t *trg, const c1_pixbuf_t *src, int y, int x) {
    assert_fatal(trg->type == src->type && trg->buf->ptr && src->buf->ptr);
    uint8_t sz = c1pb__sz(trg->type);
    // minor axis start and end mapped on trg, from src's offset x,y
    int j0 = x < 0 ? 0 : x, j1 = x + src->w < trg->w ? x + src->w : trg->w;
    if (j0 >= j1)
        return 0;
    if (trg->w_stride == 1 && src->w_stride == 1 && //
        !trg->h_inv && !trg->w_inv && !src->h_inv && !src->w_inv) {
        // if trg and src are col-compact and not inversed in index, use memcpy to speed up
        int i0 = y < 0 ? 0 : y;
        for (int i = i0; i < trg->h && i < src->h + y; i++) {
            memcpy((uint8_t *)trg->buf->ptr + (trg->h_stride * i + j0) * sz,
                   (uint8_t *)src->buf->ptr + (src->h_stride * (i - y) + (x < 0 ? -x : 0)) * sz,
                   (j1 - j0) * sz //
            );
        }
    } else {
        fatal2("unimpl"); // unimplemented now
    }
    return 0;
}

c1_pixbuf_t c1_pixbuf_dupview(const c1_pixbuf_t *pix) {
    c1_pixbuf_t res = *pix;
    if (res.buf)
        c1_sptr_incref(&res.buf);
    return res;
}

c1_pixbuf_t c1_pixbuf_fromchnl(const c1_pixbuf_t *pix, int chnl) {
    assert_fatal(chnl >= 0 && chnl <= 3);
    assert_fatal(pix->type == C1_PIXBUF_C3I8 || pix->type == C1_PIXBUF_C3I16 || //
                 pix->type == C1_PIXBUF_C3I32 || pix->type == C1_PIXBUF_C3F32);
    c1_pixbuf_t res = {pix->type - 1,
                       pix->h_inv,
                       pix->w_inv,
                       pix->h,
                       pix->w, //
                       pix->h_stride * 3,
                       pix->w_stride * 3,
                       pix->offs * 3 + chnl,
                       pix->buf};
    if (res.buf)
        c1_sptr_incref(&res.buf);
    return res;
}

void *c1_pixbuf_get(c1_pixbuf_t *pix, int y, int x) {
    y = y < 0 ? y + pix->h : y, x = x < 0 ? x + pix->w : x;
    assert_fatal_ex(y >= 0 && y < pix->h && x >= 0 && x < pix->w, //
                    "y=%d, x=%d, h=%d, w=%d", y, x, pix->h, pix->w);
    if (pix->h_inv)
        y = pix->h - 1 - y;
    if (pix->w_inv)
        x = pix->w - 1 - x;
    uint8_t *data = (uint8_t *)pix->buf->ptr;
    return data + (pix->offs + pix->h_stride * y + pix->w_stride * x) * c1pb__sz(pix->type);
}
int c1_pixbuf_clear(c1_pixbuf_t *pix) {
    if (pix->buf == NULL)
        return 0;
    if (c1_sptr_decref(&pix->buf) < 0)
        return -1;
    pix->h = pix->w = 0;
    return 0;
}

void c1_pixbuf_repr(FILE *f, const c1_pixbuf_t *pix) {
    fprintf(f, "PixelBuffer[%u, %u, %s](\r\n", pix->h, pix->w, c1pb__type2str(pix->type));
    for (int y = 0; y < pix->h; y++) {
        if (y + 10 < pix->h && y > 10) {
            fprintf(f, "  ...\r\n");
            y = pix->h - 10;
        } else {
            fprintf(f, "  [");
            for (int x = 0; x < pix->w; x++) {
                if (x + 10 < pix->w && x > 10) {
                    fprintf(f, "... ");
                    x = pix->w - 10; // no overflow
                } else {
                    switch (pix->type) {
                    case C1_PIXBUF_C1I8: {
                        const uint8_t *p = c1_pixbuf_getc(pix, y, x);
                        fprintf(f, "%02x ", *p);
                    } break;
                    case C1_PIXBUF_C1I32: {
                        const int32_t *p = c1_pixbuf_getc(pix, y, x);
                        fprintf(f, "%06d ", *p);
                    } break;
                    case C1_PIXBUF_C1I16: {
                        const int16_t *p = c1_pixbuf_getc(pix, y, x);
                        fprintf(f, "%06d ", *p);
                    } break;
                    case C1_PIXBUF_C3I8: {
                        const uint8_t *p = c1_pixbuf_getc(pix, y, x);
                        fprintf(f, "%02x%02x%02x ", p[0], p[1], p[2]);
                    } break;
                    case C1_PIXBUF_C3I16: {
                        const uint16_t *p = c1_pixbuf_getc(pix, y, x);
                        fprintf(f, "%02x%02x%02x ", p[0], p[1], p[2]);
                    } break;
                    default: {
                        fprintf(f, "? ");
                    } break;
                    }
                }
            }
            fprintf(f, "]\r\n");
        }
    }
    fprintf(f, ")\r\n");
}