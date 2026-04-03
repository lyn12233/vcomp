#include "pixbuf.h"
#include "log.h"
#include "mem.h"

#include <stdlib.h>
#include <string.h>

static uint8_t c1_pixbuf__sz(PIXBUF_TYPE t) {
    static const uint8_t map[6] = {1, 3, 2, 6, 4, 12};
    return map[t];
}
// stt,end,step to stt,nb,step
static void c1_pixbuf__norm_slice(const c1_pixbuf_t *pix, int s[3]) {
    s[0] = s[0] < 0 ? s[0] + pix->h : s[0], s[1] = s[1] < 0 ? s[1] + pix->w : s[1];
    assert_fatal(s[0] >= 0 && s[0] < pix->h && s[1] >= 0 && s[1] < pix->w && s[2] != 0);
    if (s[2] < 0) {
        int temp = s[0];
        s[0] = s[1] + 1;
        s[1] = temp + 1;
        s[2] = -s[2];
    }
    s[1] = (s[1] - s[0]) / s[2];
    s[1] = s[1] < 0 ? 0 : s[1];
}

c1_pixbuf_t c1_pixbuf_create(uint8_t type, uint16_t h, uint16_t w) {
    assert_fatal(h < C1_PIXBUF_LENMAX && w < C1_PIXBUF_LENMAX);
    void *data = malloc(h * w * c1_pixbuf__sz(type));
    assert_fatal(data);
    memset(data, 0, h * w * c1_pixbuf__sz(type));
    c1_sptr_t *p = c1_sptr_create(data, free);
    return (c1_pixbuf_t){type, 0, 0, h, w, w, 1, 0, p};
}
c1_pixbuf_t c1_pixbuf_fromview(const c1_pixbuf_t *pix, int h_slice[3], int w_slice[3]) {
    // check inverse, normalize and check h_slice
    uint8_t h_inv = h_slice[2] < 0, w_inv = w_slice[2] < 0;
    c1_pixbuf__norm_slice(pix, h_slice), c1_pixbuf__norm_slice(pix, w_slice);
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
    c1_sptr_incref(&res.buf);
    return res;
}
void *c1_pixbuf_get(c1_pixbuf_t *pix, int y, int x) {
    y = y < 0 ? y + pix->h : y, x = x < 0 ? x + pix->w : x;
    assert_fatal(y >= 0 && y < pix->h && x >= 0 && x < pix->w);
    uint8_t *data = (uint8_t *)pix->buf->ptr;
    return data + (pix->offs + pix->h_stride * y + pix->w_stride * x) * c1_pixbuf__sz(pix->type);
}
int c1_pixbuf_clear(c1_pixbuf_t *pix) {
    if (c1_sptr_incref(&pix->buf) < 0)
        return -1;
    pix->h = pix->w = 0;
    return 0;
}