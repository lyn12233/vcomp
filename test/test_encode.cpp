#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/quant.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <ios>
#include <iostream>
#include <stdio.h>

#include <opencv2/core/core.hpp>
#include <opencv2/core/types_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/opencv.hpp>

using std::cout;
using std::endl;

c1_profile_t search_prof = {0};

static void view_p16(const c1_pixbuf_t *pix, float scale = 1., int chnls = 3) {
    for (int ci = 0; ci < chnls; ci++) {
        cout << "channel " << ci << ":" << endl;
        c1_pixbuf_t c = c1_pixbuf_fromchnl(pix, ci);
        c1_pixbuf_repr(stdout, &c);
        cout << endl;

        cv::Mat tmp(pix->h, pix->w, CV_8UC1);
        for (int y = 0; y < pix->h; y++) {
            for (int x = 0; x < pix->w; x++) {
                tmp.data[y * pix->w + x] = (uint8_t)c1_clamp16(c1_abs_i16(*c1_pixbuf_geti16c(&c, y, x)), 0, 255);
                //
            }
        }
        // cv::resize(tmp, tmp, {(int)((float)tmp.size[1] * scale), (int)((float)tmp.size[2] * scale)});
        cv::imshow("", tmp);
        cv::waitKey();
        c1_pixbuf_clear(&c);
    }
}
static void view_rgb8(const c1_pixbuf_t *pix, float scale = 1., int chnls = 3) {
    cv::Mat tmp(pix->h, pix->w, CV_8UC3);
    memcpy(tmp.data, pix->buf->ptr, pix->h * pix->w * sizeof(uint8_t) * 3);
    cv::cvtColor(tmp, tmp, CV_RGB2BGR);
    cv::imshow("RGB", tmp);
    cv::waitKey();
}

int main() {
    std::ios_base::sync_with_stdio();
    c1enc_frame_t frm = {0};
    c1enc_ctx_t ctx = {0};
    c1_lookup_init_q_inf();
    const uint8_t qp = 50;

    // (1) prepare yuv frame

    cv::Mat img = cv::imread("test/img/screen_content_1.png");
    cv::cvtColor(img, img, CV_RGB2BGR);
    c1_pixbuf_t pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)img.size[0], (uint16_t)img.size[1], img.data);
    view_rgb8(&pix);
    c1_pixbuf_t pix_yuv = c1_pixbuf_cvt_rgbi8_to_yuv16(&pix);
    c1_pixbuf_clear(&pix);

    c1enc_frame_update(&frm, &pix_yuv);
    // view_img16(&frm.pix, 1, 3);

    cout << "frame size:" << frm.hgt << " " << frm.wid << endl;
    cout << "frame type:" << (int)frm.frame_type << endl;

    // (2) profile search time

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
    // search_sb decides this parm for divide/merge. not used currently?
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
    cout << endl;

    // (3) measure abosulte sum of residuals

    c1_pixbuf_t dif_vis = c1_pixbuf_create(C1_PIXBUF_C3I16, frm.pix.h, frm.pix.w);
    c1enc_get_dif(&frm, &dif_vis);
    uint64_t mat = 0;
    for (int y = 0; y < dif_vis.h; y++) {
        for (int x = 0; x < dif_vis.w; x++) {
            mat += c1_abs_i16(*c1_pixbuf_geti16c(&dif_vis, y, x));
        }
    }
    info_("prediction sad(sum of absolute of residuals): %llu", mat);

    // (4) verify prediction without tx/inv tx quant/dequant is reversible.

    for (int d = 0; d < hb && d < wb; d++) {
        for (int i = d; i < hb; i++) {
            c1pd_reconstruct_sb(frm.super_blocks + i * wb + d, &frm, &ctx);
        }
        for (int j = d; j < wb; j++) {
            c1pd_reconstruct_sb(frm.super_blocks + d * wb + j, &frm, &ctx);
        }
    }

    // view_p16(&frm.pix);
    for (int y = 0; y < pix_yuv.h; y++) {
        for (int x = 0; x < pix_yuv.w; x++) {
            assert_fatal(*c1_pixbuf_geti16c(&frm.pix, y, x) == *c1_pixbuf_geti16c(&pix_yuv, y, x));
        }
    }

    // (5) tx + quantize: gather qi. this should not be done frequently
    // this is the first pass to gather qi info. if history qi exists, gathering qi is not nece
    // to gather qi, perform a simple transform, set kth element as qstep

    c1tx_search_option_t tx_opt = {0};
    tx_opt.size_depth = 1;
    tx_opt.measure = C1TX_MEASURE_NOP;
    tx_opt.tx_rng_max = TX2TYPE_DCT_DCT + 1;
    uint64_t cnt0 = clock();
    for (int idx = 0; idx < hb * wb; idx++) {
        c1tx_search_sb(frm.super_blocks + idx, tx_opt);
        c1enc_sb_gather_qi(frm.super_blocks + idx, qp);
        c1enc_sb_dealloc_coef_bufs(frm.super_blocks + idx);
    }
    c1enc_frame_gather_qi(&frm, 75);
    info_("tx search + gather qi time: %llu", clock() - cnt0);
    c1_profile_repr(stdout, &c1tx_search_sb_prof, "min tx time", 1);
    cout << endl;

    for (int idx = 0; idx < hb * wb; idx++) {
        cout << (int)frm.super_blocks[idx].q_index_delta << " ";
    }
    cout << endl;
    info_("frame qi: %u", frm.q_index);

    // (6) verify tx is reversible

    c1_pixbuf_t dif1 = c1_pixbuf_create(C1_PIXBUF_C3I16, frm.hgt, frm.wid);
    c1_pixbuf_t dif2 = c1_pixbuf_create(C1_PIXBUF_C3I16, frm.hgt, frm.wid);
    // c1_pixbuf_t dif1 = c1_pixbuf_create(C1_PIXBUF_C3I16, 64, 64);
    // c1_pixbuf_t dif2 = c1_pixbuf_create(C1_PIXBUF_C3I16, 64, 64);

    c1enc_get_dif(&frm, &dif1);

    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm.super_blocks + idx;
        c1tx_search_sb(sb, tx_opt);
        c1tx_reconstruct(sb);
        c1enc_sb_dealloc_coef_bufs(sb);
    }

    c1enc_get_dif(&frm, &dif2);

    for (int y = 0; y < dif1.h; y++) {
        for (int x = 0; x < dif1.w; x++) {
            int16_t a = *c1_pixbuf_geti16(&dif1, y, x); // default y plane
            int16_t b = *c1_pixbuf_geti16(&dif2, y, x);
            assert_fatal_ex(c1_abs_dif_i16(a, b) < 2, "great diff at pixel (%u,%u): %d vs %d", y, x, a, b);
        }
    }

    // (7) tx again? quant and tx recon together.

    c1enc_get_dif(&frm, &dif1);
    cnt0 = clock();
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm.super_blocks + idx;
        c1tx_search_sb(sb, tx_opt);

        // c1enc_sb_gather_qi(sb, 75);
        // info_("qi: %u", sb->q_index);
        // if qi history exists, update qi selectively may be a good strategy?

        c1enc_quantize_sb(sb);
        c1enc_sb_dqc2c(sb);
        c1tx_reconstruct(sb);           // coef to dif
        c1enc_sb_dealloc_coef_bufs(sb); // write should occur before this
    }
    info_("transform, quantize, dequantize and inverse tx spend: %llu", clock() - cnt0);
    c1enc_get_dif(&frm, &dif2);

    uint32_t sum_dif = 0;
    for (int y = 0; y < dif1.h; y++) {
        for (int x = 0; x < dif1.w; x++) {
            int16_t a = *c1_pixbuf_geti16(&dif1, y, x); // default y plane
            int16_t b = *c1_pixbuf_geti16(&dif2, y, x);
            sum_dif += c1_abs_dif_i16(a, b);
        }
    }
    info_("residual distortion: %u", sum_dif);

    // pred reconstruct
    cnt0 = clock();
    for (int d = 0; d < hb && d < wb; d++) {
        for (int i = d; i < hb; i++) {
            c1pd_reconstruct_sb(frm.super_blocks + i * wb + d, &frm, &ctx);
        }
        for (int j = d; j < wb; j++) {
            c1pd_reconstruct_sb(frm.super_blocks + d * wb + j, &frm, &ctx);
        }
    }
    info_("pred recon spends: %llu", clock() - cnt0);

    sum_dif = 0;
    for (int y = 0; y < pix_yuv.h; y++) {
        for (int x = 0; x < pix_yuv.w; x++) {
            int16_t a = *c1_pixbuf_geti16(&frm.pix, y, x); // default y plane
            int16_t b = *c1_pixbuf_geti16(&pix_yuv, y, x);
            sum_dif += c1_abs_dif_i16(a, b);
        }
    }
    info_("recon distortion: %u", sum_dif);

    // view_p16(&dif1, 1, 1);
    // view_p16(&dif2, 1, 1);
    // view_p16(&dif3, 1, 1);
    // view_p16(&pix_yuv, 1, 3);
    // view_p16(&frm.pix, 1, 3);

    // (8) update context and verify it
    c1enc_push_ref(&ctx, &frm);
    for (int16_t y = 0; y < frm.pix.h; y++) {
        for (int16_t x = 0; x < frm.pix.w; x++) {
            c1enc_mv_t mvref = c1enc_get_mvref(&frm, &ctx, y / 64, x / 64, y % 64, x % 64, 1);
            // as expected zeros
            // info_("mv[%u,%u]=%+d,%+d", y, x, mvref.y, mvref.x);
        }
    }

    // view_p16(&ctx.ref_frames[0],1,1); // correct
    pix = c1_pixbuf_cvt_yuv16_to_rgbi8(&frm.pix);
    view_rgb8(&pix);
    c1_pixbuf_clear(&pix);

    // (9) next frame. test inter

    // view_p16(&ctx.ref_frames[0],1,1); // correct
    img = cv::imread("test/img/screen_content_2.png");
    pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)img.size[0], (uint16_t)img.size[1], img.data);

    // this test scene change
    // memset(pix.buf->ptr, 0, pix.h * pix.w * 3 * sizeof(int8_t));

    pix_yuv = c1_pixbuf_cvt_rgbi8_to_yuv16(&pix);
    c1_pixbuf_clear(&pix);
    c1enc_frame_update(&frm, &pix_yuv);

    // should update thre_sad_max_b to direct divide/merge? but it seems unused?

    srch_opt.try_inter = 1;
    srch_opt.inter_init_steps_mask = 16 | 4 | 1;
    srch_opt.inter_sad_subsamp_mask = 64 | 32 | 16 | 8;
    srch_opt.inter_newcand_cnt = 2;
    srch_opt.inter_ref_idx = 1;
    srch_opt.inter_only_y = 1;
    srch_opt.inter_smooth_lambda = 4;
    srch_opt.thre_inter_efficient_sad = 3;
    srch_opt.thre_skip_inter_sad = 64;
    srch_opt.thre_intra_efficient_sad = 10;

    c1_profile_reset(&c1pd_predict_prof);
    cnt0 = clock();
    c1_pixbuf_t dif3 = c1_pixbuf_create(C1_PIXBUF_C3I16, 64, 64);
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm.super_blocks + idx;
        c1_profile_enter(&search_prof);
        // c1enc_sb_repr(stdout,sb,0);
        c1enc_search_sb(sb, &frm.pix, &ctx, &srch_opt);

        // c1enc_get_dif_sb(sb,&dif3);
        c1enc_sb_repr(stdout, sb, 0);
        // view_p16(&dif3,1,1);

        c1enc_part_gather_pred_type(sb->root);
        c1enc_part_gather_residual(sb->root, &frm.pix, &ctx, 1);
        c1_profile_exit(&search_prof);

        // c1enc_get_dif_sb(sb,&dif3);
        // view_p16(&dif3,1,1);
    }

    debug("inter search elapse: %llu", clock() - cnt0);
    info_("predict profile:");
    c1_profile_repr(stdout, &c1pd_predict_prof, "prediction times", 0);
    cout << endl;
    // c1enc_get_dif(&frm, &dif2);
    // view_p16(&dif2, 1, 1);

    // (10) verify prediction is reversible

    for (int d = 0; d < hb && d < wb; d++) {
        for (int i = d; i < hb; i++) {
            c1pd_reconstruct_sb(frm.super_blocks + i * wb + d, &frm, &ctx);
        }
        for (int j = d; j < wb; j++) {
            c1pd_reconstruct_sb(frm.super_blocks + d * wb + j, &frm, &ctx);
        }
    }
    // view_p16(&pix_yuv, 1, 1);
    // view_p16(&frm.pix, 1, 1);

    int nwarn = 0;
    for (int y = 0; y < pix_yuv.h; y++) {
        for (int x = 0; x < pix_yuv.w; x++) {
            int16_t a = *c1_pixbuf_geti16c(&frm.pix, y, x);
            int16_t b = *c1_pixbuf_geti16c(&pix_yuv, y, x);
            if (c1_abs_dif_i16(a, b) > 50) {
                warning("great dev: %u -> %u", b, a);
                nwarn++;
            }
            if (nwarn > 100)
                break;
        }
        if (nwarn > 100)
            break;
    }
    assert_fatal_ex(nwarn <= 100, "too much devs");

    // (11) tx quant & inv

    cnt0 = clock();
    for (int idx = 0; idx < hb * wb; idx++) {
        c1enc_super_block_t *sb = frm.super_blocks + idx;
        c1tx_search_sb(sb, tx_opt);

        c1enc_sb_gather_qi(sb, qp);

        c1enc_quantize_sb(sb);
        c1enc_sb_dqc2c(sb);
        c1tx_reconstruct(sb);           // coef to dif
        c1enc_sb_dealloc_coef_bufs(sb); // write should occur before this
        // c1enc_sb_repr(stdout, sb, 0);
    }
    info_("transform, quantize, dequantize and inverse tx spend: %llu", clock() - cnt0);

    cnt0 = clock();
    for (int d = 0; d < hb && d < wb; d++) {
        for (int i = d; i < hb; i++) {
            c1pd_reconstruct_sb(frm.super_blocks + i * wb + d, &frm, &ctx);
        }
        for (int j = d; j < wb; j++) {
            c1pd_reconstruct_sb(frm.super_blocks + d * wb + j, &frm, &ctx);
        }
    }
    info_("pred recon spends: %llu", clock() - cnt0);

    sum_dif = 0;
    for (int y = 0; y < pix_yuv.h; y++) {
        for (int x = 0; x < pix_yuv.w; x++) {
            int16_t a = *c1_pixbuf_geti16(&frm.pix, y, x); // default y plane
            int16_t b = *c1_pixbuf_geti16(&pix_yuv, y, x);
            sum_dif += c1_abs_dif_i16(a, b);
        }
    }
    info_("recon distortion: %u", sum_dif);
    c1enc_push_ref(&ctx, &frm);
    for (int16_t y = 64; y < frm.pix.h; y++) {
        for (int16_t x = 64; x < frm.pix.w; x++) {
            c1enc_mv_t mvref = c1enc_get_mvref(&frm, &ctx, y / 64, x / 64, y % 64, x % 64, 1);
            // info_("mv[%u,%u] %d,%d", y, x,mvref.y,mvref.x);
        }
    }

    view_p16(&frm.pix, 1, 1);

    c1_pixbuf_clear(&pix_yuv);
    c1enc_frame_clear(&frm);
}