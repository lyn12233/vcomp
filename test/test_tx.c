#include "encode/types.h"
#include "math/transform.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <stdint.h>
#include <math.h>

const int16_t data[64] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, //
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xffff, 0xfff8, 0xfff6, //
    0xfff7, 0xfff6, 0xfff6, 0xfff6, 0xfff6, 0xfff6, 0xfff6, 0xfff5, //
    0xfff5, 0xfff5, 0xfff5, 0xfff6, 0xfff6, 0xfff6, 0xfff6, 0xfff6, //
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x0001, 0x0000, //
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0xffff, 0xfff8, 0xfff6, //
    0xfff7, 0xff02, 0xff02, 0xff02, 0xfff6, 0xfff6, 0xfff6, 0xfff5, //
    0xfff5, 0xfff5, 0xfff5, 0xfff6, 0xfff6, 0xfff6, 0xfff5, 0xfff6  //
};

int main() {
    c1_pixbuf_t p = c1_pixbuf_from_ptr(C1_PIXBUF_C1I16, 8, 8,data);
    c1_pixbuf_t p2 = c1_pixbuf_create(C1_PIXBUF_C1I32, 8,8);
    info_("origin matrix:");
    c1_pixbuf_repr(stdout, &p);

    c1tx_option_t opt = {C1_SZ_8_8, TX2TYPE_DCT_DCT};

    c1tx_txfm2d(p.buf->ptr, p2.buf->ptr, &opt);
    info_("after dct-dct:");
    c1_pixbuf_repr(stdout, &p2);

    c1tx_inv_txfm2d(p2.buf->ptr, p.buf->ptr, &opt);
    info_("inverse result:");
    c1_pixbuf_repr(stdout, &p);

    c1_pixbuf_clear(&p), c1_pixbuf_clear(&p2);
    // c1_pixbuf_t p = c1_pixbuf_create(C1_PIXBUF_C1I16, 16, 16);
    // c1_pixbuf_t p2 = c1_pixbuf_create(C1_PIXBUF_C1I32, 16, 16);
    // *c1_pixbuf_geti16(&p, 0, 0) = 255;
    // info_("origin matrix:");
    // c1_pixbuf_repr(stdout, &p);

    // c1tx_option_t opt = {C1_SZ_16_16, TX2TYPE_DCT_DCT};

    // c1tx_txfm2d(p.buf->ptr, p2.buf->ptr, &opt);
    // info_("after h-dct:");
    // c1_pixbuf_repr(stdout, &p2);

    // c1tx_inv_txfm2d(p2.buf->ptr, p.buf->ptr, &opt);
    // info_("inverse result:");
    // c1_pixbuf_repr(stdout, &p);

    // c1_pixbuf_clear(&p), c1_pixbuf_clear(&p2);
}