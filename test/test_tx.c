#include "src/math/transform.h"
#include "util/pixbuf.h"

int main() {
    c1_pixbuf_t p = c1_pixbuf_create(PIXBUF_C1I32, 8, 8);
    *c1_pixbuf_geti32(&p, 0, 0) = 255;
    c1_pixbuf_repr(stdout, &p);
    c1tx_option_t opt = {TX2SZ_8_8, TX2TYPE_DCT_DCT};
    int32_t buf[64];
    c1tx_txfm2d(&p, &p, &opt, buf);
    c1_pixbuf_repr(stdout, &p);
}