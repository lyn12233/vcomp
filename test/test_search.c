#include "encode/types.h"
#include "src/encode/predictor.h"
#include "util/log.h"
#include "util/mem.h"
#include "util/pixbuf.h"

#include <stdio.h>
#include <string.h>

int main() {
    int16_t diff_buf[64];
    c1enc_block_t b = {0, C1_SZ_8_8, 0, 0, 0, 0};

    c1_pixbuf_t pix = c1_pixbuf_create(C1_PIXBUF_C3I16, 16, 16);
    c1pd_option_t opt = {C1_SZ_8_8};
    for (uint8_t off = 0; off < 16; off += 8) {
        b.xoff = b.yoff = off;
        for (C1_PRED_MODE mode = C1_PRED_MVNEW; mode <= C1_PRED_PAETH; mode++) {
            info_("mode %d:", mode);
            opt.mode = mode;
            c1pd_predict(&b, diff_buf, &pix, &opt, NULL);
            c1_dump_buf(diff_buf, 8 * 8);
        }
    }
    c1_pixbuf_clear(&pix);
}