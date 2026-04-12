#include "src/math/transform.h"
#include "src/util/log.h"
#include "util/pixbuf.h"

#include <math.h>

int main() {
    c1_pixbuf_t p = c1_pixbuf_create(PIXBUF_C1I32, 8, 8);
    c1_pixbuf_t p2 = c1_pixbuf_create(PIXBUF_C1I32, 8, 8);
    *c1_pixbuf_geti32(&p, 0, 0) = 255;
    info("origin matrix:");
    c1_pixbuf_repr(stdout, &p);
    c1tx_option_t opt = {TX2SZ_8_8, TX2TYPE_H_DCT};
    int32_t buf[64];
    c1tx_txfm2d(&p, &p, &opt, buf);
    info("after h-dct:");
    c1_pixbuf_repr(stdout, &p);

    // reference_dct_1d(c1_pixbuf_get(&p, 0, 0), c1_pixbuf_get(&p2, 0, 0), 4);
    // info("reference:");
    // c1_pixbuf_repr(stdout, &p2);

    c1tx_inv_txfm2d(&p, &p2, &opt, buf);
    info("inverse result:");
    c1_pixbuf_repr(stdout, &p2);
}