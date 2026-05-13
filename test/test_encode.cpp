#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ios>
#include <iostream>

#include <opencv2/core/core.hpp>
#include <opencv2/core/types_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using std::cout;
using std::endl;

c1_profile_t search_prof = {0};

static void view_p16(const c1_pixbuf_t *pix) {
    for (int ci = 0; ci < 3; ci++) {
        cout << "channel " << ci << ":" << endl;
        c1_pixbuf_t c = c1_pixbuf_fromchnl(pix, ci);
        c1_pixbuf_repr(stdout, &c);
        cout << endl;

        cv::Mat tmp(pix->h, pix->w, CV_8UC1);
        for (int y = 0; y < pix->h; y++) {
            for (int x = 0; x < pix->w; x++) {
                tmp.data[y * pix->w + x] = (uint8_t)c1_clamp16(c1_abs_i16(*c1_pixbuf_geti16c(pix, y, x)), 0, 255);
                //
            }
        }
        cv::imshow("", tmp);
        cv::waitKey();
        c1_pixbuf_clear(&c);
    }
}

int main() {
    std::ios_base::sync_with_stdio();
    c1enc_frame_t frm = {0};
    c1enc_ctx_t ctx = {0};

    cv::Mat img = cv::imread("test/img/sreen_context_1.png");
    c1_pixbuf_t pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)img.size[0], (uint16_t)img.size[1], img.data);
    c1_pixbuf_t pix_yuv = c1_pixbuf_cvt_rgbi8_to_yuv16(&pix);
    c1_pixbuf_clear(&pix);

    c1enc_frame_update(&frm, &pix_yuv);
    cout << "frame size:" << frm.hgt << " " << frm.wid << endl;
    cout << "frame type:" << (int)frm.frame_type << endl;

    c1enc_search_option_t srch_opt = {0};
    srch_opt.try_intra = 1;
    srch_opt.intra_try_uv = 0;
    srch_opt.intra_try_cfl = 0;
    srch_opt.intra_rng_max = C1_PRED_PAETH + 1;
    srch_opt.thre_mode_better_mult = 3;
    srch_opt.thre_mode_better_shift = 1;
    srch_opt.thre_mat_is_dif_mult = 3;
    srch_opt.thre_mat_is_dif_shift = 2;
    srch_opt.thre_mat_is_dif_delta = 1 << 12;
    // todo: impl
    srch_opt.thre_sad_max_b = 1 << 20;

    // search in a order that ensure prediction edges(at least bh+bw<=64*2) exist ?
    const int hb = frm.hgt_per_sb, wb = frm.wid_per_sb;

    for (int idx = 0; idx < hb * wb; idx++) {
        c1_profile_enter(&search_prof);
        c1enc_search_sb(frm.super_blocks + idx, &frm.pix, &ctx, &srch_opt);
        // gather mode and diff
        c1enc_part_gather_pred_type(frm.super_blocks[idx].root);
        c1enc_part_gather_residual(frm.super_blocks[idx].root, &frm.pix, &ctx, 0);
        // info_("searched %u", idx);
        c1_profile_exit(&search_prof);
    }

    c1_profile_repr(stdout, &c1enc_search_sb_prof, "prediction search", 4);
    cout << endl;
    c1_profile_repr(stdout, &c1enc_search_is_divide_prof, "divide times", 0);
    cout << endl;
    c1_profile_repr(stdout, &search_prof, "search times", 0);
    cout << endl;
    c1_profile_repr(stdout, &c1pd_predict_prof, "predict times", 0);

    c1_pixbuf_t dif_vis = c1_pixbuf_create(C1_PIXBUF_C3I16, frm.pix.h, frm.pix.w);
    c1enc_get_dif(&frm, &dif_vis);
    uint64_t mat = 0;
    for (int y = 0; y < dif_vis.h; y++) {
        for (int x = 0; x < dif_vis.w; x++) {
            mat += c1_abs_i16(*c1_pixbuf_geti16c(&dif_vis, y, x));
        }
    }
    info_("sad: %llu", mat);
    // view_p16(&dif_vis);
    view_p16(&frm.pix);

    mat = 0;
    for (int d = 0; d < hb && d < wb; d++) {
        for (int i = d; i < hb; i++) {
            c1pd_reconstruct_sb(frm.super_blocks + i * wb + d, &frm, &ctx);
        }
        for (int j = d; j < wb; j++) {
            c1pd_reconstruct_sb(frm.super_blocks + d * wb + j, &frm, &ctx);
        }
    }

    view_p16(&frm.pix);
    for (int y = 0; y < pix_yuv.h; y++) {
        for (int x = 0; x < pix_yuv.w; x++) {
            mat += c1_abs_i16(*c1_pixbuf_geti16c(&pix_yuv, y, x) - *c1_pixbuf_geti16c(&frm.pix, y, x));
        }
    }
    info_("sad: %llu", mat);

    c1_pixbuf_clear(&pix_yuv);
    c1enc_frame_clear(&frm);
}