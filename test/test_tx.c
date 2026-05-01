#include "encode/types.h"
#include "math/transform.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <math.h>

int main() {
    c1_pixbuf_t p = c1_pixbuf_create(C1_PIXBUF_C1I16, 16, 16);
    c1_pixbuf_t p2 = c1_pixbuf_create(C1_PIXBUF_C1I32, 16, 16);
    *c1_pixbuf_geti16(&p, 0, 0) = 255;
    info_("origin matrix:");
    c1_pixbuf_repr(stdout, &p);

    c1tx_option_t opt = {C1_SZ_16_16, TX2TYPE_DCT_DCT};

    c1tx_txfm2d(p.buf->ptr, p2.buf->ptr, &opt);
    info_("after h-dct:");
    c1_pixbuf_repr(stdout, &p2);

    c1tx_inv_txfm2d(p2.buf->ptr, p.buf->ptr, &opt);
    info_("inverse result:");
    c1_pixbuf_repr(stdout, &p);

    c1_pixbuf_clear(&p), c1_pixbuf_clear(&p2);
}