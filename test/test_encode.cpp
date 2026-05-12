#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <iostream>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using std::cout;
using std::endl;

int main() {
    std::ios_base::sync_with_stdio();
    c1enc_frame_t frm = {0};
    c1enc_ctx_t ctx = {0};

    cv::Mat img = cv::imread("test/img/sreen_context_1.png");
    c1_pixbuf_t pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)img.size[0], (uint16_t)img.size[1], img.data);
    c1_pixbuf_t pix_yuv = c1_pixbuf_cvt_rgbi8_to_yuv16(&pix);
    c1_pixbuf_clear(&pix);

    c1enc_frame_update(&frm, &pix_yuv);
    c1_pixbuf_clear(&pix_yuv);
    cout << "frame size:" << frm.hgt << " " << frm.wid << endl;
    cout << "frame type:" << (int)frm.frame_type << endl;

    c1enc_search_option_t srch_opt = {0};
    srch_opt.try_intra = 1;
    srch_opt.intra_try_uv = 1;
    srch_opt.intra_try_cfl = 0;
    srch_opt.intra_rng_max = C1_PRED_PAETH + 1;
    srch_opt.thre_mode_better_mult = 3;
    srch_opt.thre_mode_better_shift = 1;
    srch_opt.thre_mat_is_dif_mult = 3;
    srch_opt.thre_mat_is_dif_shift = 2;
    srch_opt.thre_mat_is_dif_delta = 1 << 12;
    // todo: impl
    srch_opt.thre_sad_max_b = 1 << 12;

    // search in a order that ensure prediction edges(at least bh+bw<=64*2) exist ?
    const int hb = frm.hgt_per_sb, wb = frm.wid_per_sb;

    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_search_sb(frm.super_blocks + idx, &frm.pix, &ctx, &srch_opt);
        // gather mode and diff
        c1enc_part_gather_pred_type(frm.super_blocks[idx].root);
        c1enc_part_gather_residual(frm.super_blocks[idx].root, &frm.pix, &ctx, 0);
        // info_("searched %u", idx);
    }

    c1enc_frame_clear(&frm);

    c1_profile_repr(stdout, &c1enc_search_sb_prof, "prediction search", 4);
}