#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/quant.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/log.h"
#include "util/pixbuf.h"

int main() {
    c1enc_frame_t frm = {0};
    c1enc_ctx_t ctx = {0};
    c1_pixbuf_t pix_yuv = c1_pixbuf_create(C1_PIXBUF_C3I16, 1472, 960);
    c1enc_frame_update(&frm, &pix_yuv);

    c1tx_search_option_t tx_opt = {0};
    tx_opt.size_depth = 1;
    tx_opt.measure = C1TX_MEASURE_NOP;
    tx_opt.tx_rng_max = TX2TYPE_DCT_DCT + 1;
    const int hb = frm.hgt_per_sb, wb = frm.wid_per_sb;
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm.super_blocks + idx;
        c1tx_search_sb(sb, tx_opt);
        c1tx_reconstruct(sb);
    }
}