#include "encode/encoder.h"
#include "encode/predictor.h"
#include "encode/search.h"
#include "encode/types.h"
#include "util/pixbuf.h"

#include <assert.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/opencv.hpp>

using std::cout;

#define C1__FRAME_SAD_MAX_DEV_SCALE 4

static uint32_t c1enc__search_decide_sad_max(uint32_t a, uint32_t b) {
    if (b < a / C1__FRAME_SAD_MAX_DEV_SCALE) {
        return a / C1__FRAME_SAD_MAX_DEV_SCALE;
    }
    if (b / C1__FRAME_SAD_MAX_DEV_SCALE > a) {
        // avoid overflow
        return UINT32_MAX / C1__FRAME_SAD_MAX_DEV_SCALE > a ? a * C1__FRAME_SAD_MAX_DEV_SCALE : UINT32_MAX;
    }
    return b;
}

int main() {
    std::ios_base::sync_with_stdio();
    auto img = cv::imread("test/img/placeholder.jpg");
    cv::Mat im2;
    cv::resize(img, im2, {128, 128});
    // cv::imshow("test",im2);
    // cv::waitKey();
    cout << im2.channels();
    void *p = im2.data;
    c1_pixbuf_t pix = c1_pixbuf_from_ptr(C1_PIXBUF_C3I8, (uint16_t)im2.size[0], (uint16_t)im2.size[1], im2.data);
    c1_pixbuf_repr(stdout, &pix);
    c1_pixbuf_t pix16 = c1_pixbuf_cvt(&pix, C1_PIXBUF_C3I16);
    c1enc_frame_t frm = {0};
    c1enc_frame_update(&frm, &pix16);
    assert_fatal(frm.inf.hgt_per_sb == 2 && frm.inf.wid_per_sb == 2);
    c1enc_super_block_t *sb = frm.super_blocks + 2;
    c1enc_ctx_t ctx = {0};

    c1enc_search_option_t opt = {0};
    opt.try_intra = 1;
    opt.intra_rng_max = C1_PRED_PAETH + 1;
    opt.thre_mode_better_mult = 3; // 1.5
    opt.thre_mode_better_shift = 1;
    opt.thre_mat_is_dif_mult = 3; // 0.75
    opt.thre_mat_is_dif_shift = 2;
    opt.thre_mat_is_dif_delta = 16 * 16 * 16 * 3;
    opt.thre_sad_max_b = 1 << 12;

    c1enc_search_option_validate(&opt);
    assert_fatal(c1enc_search_p(sb->root, &frm.pix, &ctx, &opt) >= 0);
    // c1enc_sb_repr(stdout, sb, 0);

    assert_fatal(c1enc_part_gather_rdstat(sb->root) >= 0);
    info_("total SAD: %u", sb->root->stats.sad);
    opt.thre_sad_max_b = c1enc__search_decide_sad_max(sb->root->stats.sad / 16, opt.thre_sad_max_b);
    info_("new sad threshold: %u", opt.thre_sad_max_b);

    assert_fatal(c1enc_search_merge(sb->root, &frm.pix, &ctx, &opt) >= 0);
    assert_fatal(c1enc_search_divide(sb->root, &frm.pix, &ctx, &opt) >= 0);
    assert_fatal(c1enc_part_gather_rdstat(sb->root) >= 0);
    info_("total SAD: %u", sb->root->stats.sad);
    assert_fatal(c1enc_part_gather_pred_type(sb->root) >= 0);
    c1enc_sb_repr(stdout, sb, 0);

    assert_fatal(c1enc_part_gather_residual(sb->root, &frm.pix, &ctx, 0) >= 0);
    c1_pixbuf_t dif = c1enc_get_dif_sb(sb);
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            for (int ci = 0; ci < 3; ci++) {
                int16_t *ptr = c1_pixbuf_geti16(&dif, i, j) + ci;
                *ptr = (*ptr) > 0 ? (*ptr) : (int16_t)-(*ptr);
            }
        }
    }
    c1_pixbuf_t dif8 = c1_pixbuf_cvt(&dif, C1_PIXBUF_C3I8);
    cv::Mat im3(64, 64, CV_8UC3);
    memcpy(im3.data, dif8.buf->ptr, 64 * 64 * 3);
    cv::resize(im3, im3, {256, 256});
    cv::imshow("pred", im3);
    // cv::waitKey();

    c1_pixbuf_clear(&pix);
    c1_pixbuf_clear(&pix16);
    c1_pixbuf_clear(&dif);
    c1_pixbuf_clear(&dif8);
    c1enc_frame_clear(&frm);

    // test2: intra dir pred test 8x8
    int16_t above[16] = {0, 0, 0, 0, 0, 255, 255, 255, 255};
    int16_t left[16] = {0, 0, 0, 0, 0, 255, 255, 255, 255};
    cv::Mat pred_result(8, 8, CV_16UC1);
    c1pd_intra_preds[C1_PRED_PAETH - C1_PRED_DC - 1][C1_SZ_8_8](NULL, (int16_t *)pred_result.data, above + 1, left + 1);
    cout << pred_result;
    // cv::resize(pred_result, pred_result, {256, 256});
    // cv::imshow("pred result", pred_result);
    cv::waitKey();
}