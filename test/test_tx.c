#include "src/math/transform.h"
#include "src/util/log.h"
#include "util/pixbuf.h"

#include <math.h>

int main() {
    c1_pixbuf_t p = c1_pixbuf_create(C1_PIXBUF_C1I16, 8, 8);
    c1_pixbuf_t p2 = c1_pixbuf_create(C1_PIXBUF_C1I32, 8, 8);
    *c1_pixbuf_geti16(&p, 0, 0) = 255;
    info("origin matrix:");
    c1_pixbuf_repr(stdout, &p);

    c1tx_option_t opt = {C1_SZ_8_8, TX2TYPE_DCT_DCT};
    int32_t buf[64];
    c1tx_txfm2d(&p, &p2, &opt, buf);
    info("after h-dct:");
    c1_pixbuf_repr(stdout, &p2);

    c1tx_inv_txfm2d(&p2, &p, &opt, buf);
    info("inverse result:");
    c1_pixbuf_repr(stdout, &p);

    c1_pixbuf_clear(&p),c1_pixbuf_clear(&p2);
}