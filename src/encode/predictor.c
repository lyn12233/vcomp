#include "predictor.h"
#include "types.h"

#include "util/log.h"
#include "util/mem.h"
#include "util/pixbuf.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* table of contents
    - intra predictor cores
        - dc's:                     50
        - 90deg directions:         60
        - paeth:                    90
        - 22.5/45 directions z1:    110
        - 22.5/45 directions z2:    140
        - 22.5/45 directions z3:    160
        - tangent lookup:           200
        - 22.5/45 all directions:   200
        - uniform dir pred cores:   250
        - pred core arrays:         270
        - intra pred without cfl:   300
        - inter pred:               380
        - intra cfl:                400
        - all in one:               460
*/

// --- utils ---

static void c1pd__memset16(int16_t *output, int16_t val, int nb) {
    for (int i = 0; i < nb; i++)
        output[i] = val;
}
static int16_t c1pd__abs_dif(int16_t a, int16_t b) {
    return a > b ? a - b : b - a;
}
static int c1pd__clamp(int a, int lb, int ub) {
    return a < lb ? lb : a > ub ? ub : a;
};

// --- intra predictor core impls ---

static void c1pd__dc_128(const int16_t *input, int16_t *output,     //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w) {
    c1pd__memset16(output, 128, h * w);
}
static void c1pd__dc_top(const int16_t *input, int16_t *output,     //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w) {
    int16_t sum = 0;
    for (int i = 0; i < w; i++)
        sum += above[i];
    int16_t dc = C1_ROUND_MID(sum, w);
    c1pd__memset16(output, dc, h * w);
}
static void c1pd__dc_left(const int16_t *input, int16_t *output,     //
                          const int16_t *above, const int16_t *left, //
                          uint8_t h, uint8_t w) {
    int16_t sum = 0;
    for (int i = 0; i < h; i++)
        sum += left[i];
    int16_t dc = C1_ROUND_MID(sum, h);
    c1pd__memset16(output, dc, h * w);
}
static void c1pd__dc(const int16_t *input, int16_t *output,     //
                     const int16_t *above, const int16_t *left, //
                     uint8_t h, uint8_t w) {
    int16_t sum = 0;
    for (int i = 0; i < h; i++)
        sum += left[i];
    for (int j = 0; j < w; j++)
        sum += above[j];
    int16_t dc = C1_ROUND_MID(sum, h + w);
    c1pd__memset16(output, dc, h * w);
}
static void c1pd__v_pred(const int16_t *input, int16_t *output,     //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w) {
    for (int i = 0; i < h; i++) {
        memcpy(output + w * i, above, w * sizeof(int16_t));
    }
}
static void c1pd__h_pred(const int16_t *input, int16_t *output,     //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w) {
    for (int i = 0; i < h; i++) {
        c1pd__memset16(output + w * i, left[i], w);
    }
}
static int16_t c1pd__paeth_pred_pix(int16_t left, int16_t top, int16_t top_left) {
    int16_t base = top + left - top_left;
    int16_t pleft = c1pd__abs_dif(base, left), ptop = c1pd__abs_dif(base, top),
            ptop_left = c1pd__abs_dif(base, top_left);
    return (pleft <= ptop && pleft <= ptop_left) ? left : (ptop <= ptop_left) ? top : top_left;
}
static void c1pd__paeth(const int16_t *input, int16_t *output,     //
                        const int16_t *above, const int16_t *left, //
                        uint8_t h, uint8_t w) {
    int16_t top_left = above[-1];
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            output[w * i + j] = c1pd__paeth_pred_pix(left[i], above[j], top_left);
        }
    }
}
// directional prediction zone 1 90~180, angle measure to the axis +x.
// delta(y,x)=(1,dx)
static void c1pd__dir_z1(int16_t *output,                           //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w, int dx, int dy) {
    const int base_max = h + w - 1;
    const int frac_bits = 6;
    const int base_inc = 1;
    for (int i = 0; i < h; i++) {
        int x = (i + 1) * dx; // measures the x offset on "above" row
        int base = x >> frac_bits;
        const int shift = (x & 0x3f) >> 1;
        if (base >= base_max) {
            for (int i2 = i; i2 < h; i2++) {
                c1pd__memset16(output + w * i2, above[base_max], w);
            }
            break;
        }
        for (int j = 0; j < w; j++) {
            if (base < base_max) {
                int val = above[base] * (32 - shift) + above[base + 1] * shift;
                output[w * i + j] = (int16_t)C1_ROUND_BITS(val, 5);
            } else {
                output[w * i + j] = above[base_max];
            }
            base += base_inc; // x+=(1<<6)
        }
    }
}
// directional prediction zone 2 90~180, uses both left and above
static void c1pd__dir_z2(int16_t *output,                           //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w, int dx, int dy) {
    const int base_x_min = -1;
    const int base_y_min = -1;
    const int frac_bits_x = 6;
    const int frac_bits_y = 6;
    // 2 kinds of offset: ref above: delta(y,x)=(1,-dx); ref left: delta(y,x)=(-dy,1)
    // int x=(1<<6)-dx,y=dy;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int val;
            int x = j * (1 << 6) - (i + 1) * dx;
            const int base_x = x >> frac_bits_x;

            if (base_x >= base_x_min) {
                const int shift = (x & 0x3f) >> 1;
                val = above[base_x] * (32 - shift) + above[base_x + 1] * shift;

            } else {
                int y = i * (1 << 6) - (j + 1) * dy;
                const int base_y = y >> frac_bits_y;

                const int shift = (y & 0x3f) >> 1;
                val = left[base_y] * (32 - shift) + left[base_y + 1] * shift;
            }
            output[w * i + j] = (int16_t)C1_ROUND_BITS(val, 5);
        }
    }
}
// dierctional prediction zone 3 90~270 delta(y,x)=(dy,1)
static void c1pd__dir_z3(int16_t *output,                           //
                         const int16_t *above, const int16_t *left, //
                         uint8_t h, uint8_t w, int dx, int dy) {
    const int base_max = h + w - 1;
    const int frac_bits = 6;
    const int base_inc = 1;
    for (int j = 0; j < w; j++) {
        int y = (j + 1) * dy;
        int base = y >> frac_bits;
        const int shift = (y & 0x3f) >> 1;
        for (int i = 0; i < h; i++) {
            if (base < base_max) {
                int val = left[base] * (32 - shift) + left[base + 1] * shift;
                output[w * i + j] = (int16_t)C1_ROUND_BITS(val, 5);
            } else {
                while (i < h) {
                    output[w * i + j] = left[base_max];
                    i++;
                }
                break;
            }
            base += base_inc;
        }
    }
}
// co-tangents 0-90deg with 6 bit fraction, for direction predition
static const int16_t c1pd__tans[4] = {
    0,   // 0 deg, placeholder
    151, // 23deg
    64,  // 45deg
    27,  // 67deg
};
// pred mode to degree index, d45..d157 -> 23deg...157deg indices
// roughly per 22.5 deg
static const uint8_t c1pd__tan_lookup[6] = {
    // d45 d135 d67 d113 d157 d203
    2, 6, 3, 5, 7, 9 //
};
static void c1pd__dir(int16_t *output,                           //
                      const int16_t *above, const int16_t *left, //
                      uint8_t h, uint8_t w, C1_PRED_TYPE type) {
    uint8_t angle_idx = c1pd__tan_lookup[type - C1_PRED_D45];
    if (angle_idx > 0 && angle_idx < 4) {
        c1pd__dir_z1(output, above, left, h, w, c1pd__tans[angle_idx], 1);
    } else if (angle_idx > 4 && angle_idx < 8) {
        c1pd__dir_z2(output, above, left, h, w, c1pd__tans[8 - angle_idx], c1pd__tans[angle_idx - 4]);
    } else if (angle_idx > 8 && angle_idx < 12) {
        c1pd__dir_z3(output, above, left, h, w, 1, c1pd__tans[12 - angle_idx]);
    } else {
        fatal2("unreachable");
    }
}

#define C1_PRED_NAME_1(name, h, w) c1pd__##name##_##h##_##w
#define C1_PRED_NAME_2(name, h, w, angle) c1pd__##name##_##angle##_##h##_##w

#define C1_MAKE_PRED_1(name, h, w)                                                    \
    static void c1pd__##name##_##h##_##w(const int16_t *input, int16_t *output, /**/  \
                                         const int16_t *above, const int16_t *left) { \
        c1pd__##name(input, output, above, left, h, w);                               \
    }

#define C1_MAKE_PRED_2(name, h, w, angle)                                                       \
    static void c1pd__##name##_##angle##_##h##_##w(const int16_t *input, int16_t *output, /**/  \
                                                   const int16_t *above, const int16_t *left) { \
        c1pd__##name(output, above, left, h, w, C1_PRED_D##angle);                              \
    }

#define C1_MAKE_PREDS_1(name)     \
    C1_MAKE_PRED_1(name, 8, 8);   \
    C1_MAKE_PRED_1(name, 16, 16); \
    C1_MAKE_PRED_1(name, 32, 32); \
    C1_MAKE_PRED_1(name, 64, 64);
#define C1_MAKE_PRED_2_ALL_SIZES(name, angle) \
    C1_MAKE_PRED_2(name, 8, 8, angle);        \
    C1_MAKE_PRED_2(name, 16, 16, angle);      \
    C1_MAKE_PRED_2(name, 32, 32, angle);      \
    C1_MAKE_PRED_2(name, 64, 64, angle);
#define C1_MAKE_PREDS_2(name)            \
    C1_MAKE_PRED_2_ALL_SIZES(name, 45);  \
    C1_MAKE_PRED_2_ALL_SIZES(name, 135); \
    C1_MAKE_PRED_2_ALL_SIZES(name, 67);  \
    C1_MAKE_PRED_2_ALL_SIZES(name, 113); \
    C1_MAKE_PRED_2_ALL_SIZES(name, 157); \
    C1_MAKE_PRED_2_ALL_SIZES(name, 203);

C1_MAKE_PREDS_1(dc_128);
C1_MAKE_PREDS_1(dc_top);
C1_MAKE_PREDS_1(dc_left);
C1_MAKE_PREDS_1(dc);
C1_MAKE_PREDS_1(v_pred);
C1_MAKE_PREDS_1(h_pred);
C1_MAKE_PREDS_1(paeth);
C1_MAKE_PREDS_2(dir);

c1pd_intra_func_t c1pd_intra_preds[C1_PD_INTRA_CNT - 1][C1_SIZE_CNT] = {
    {c1pd__h_pred_8_8, c1pd__h_pred_16_16, c1pd__h_pred_32_32, c1pd__h_pred_64_64},
    {c1pd__v_pred_8_8, c1pd__v_pred_16_16, c1pd__v_pred_32_32, c1pd__v_pred_64_64},
    {c1pd__dir_45_8_8, c1pd__dir_45_16_16, c1pd__dir_45_32_32, c1pd__dir_45_64_64},
    {c1pd__dir_135_8_8, c1pd__dir_135_16_16, c1pd__dir_135_32_32, c1pd__dir_135_64_64},
    {c1pd__dir_67_8_8, c1pd__dir_67_16_16, c1pd__dir_67_32_32, c1pd__dir_67_64_64},
    {c1pd__dir_113_8_8, c1pd__dir_113_16_16, c1pd__dir_113_32_32, c1pd__dir_113_64_64},
    {c1pd__dir_157_8_8, c1pd__dir_157_16_16, c1pd__dir_157_32_32, c1pd__dir_157_64_64},
    {c1pd__dir_203_8_8, c1pd__dir_203_16_16, c1pd__dir_203_32_32, c1pd__dir_203_64_64},
    {c1pd__paeth_8_8, c1pd__paeth_16_16, c1pd__paeth_32_32, c1pd__paeth_64_64},
};
// first index: has_left
c1pd_intra_func_t c1pd_dc_preds[2][2][C1_SIZE_CNT] = {
    {
        {c1pd__dc_128_8_8, c1pd__dc_128_16_16, c1pd__dc_128_32_32, c1pd__dc_128_64_64},
        {c1pd__dc_top_8_8, c1pd__dc_top_16_16, c1pd__dc_top_32_32, c1pd__dc_top_64_64},
    },
    {
        {c1pd__dc_left_8_8, c1pd__dc_left_16_16, c1pd__dc_left_32_32, c1pd__dc_left_64_64},
        {c1pd__dc_8_8, c1pd__dc_16_16, c1pd__dc_32_32, c1pd__dc_64_64},
    },
};

static const int16_t c1pd__128_border[128] = {
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128,
};

static int c1pd__predict_intra(const c1enc_block_t *b, int16_t *output, //
                               const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    // 0. abbrv attrs from b
    const uint8_t bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int border_x = (int)b->sb_x * 64 + b->xoff - 1;
    const int border_y = (int)b->sb_y * 64 + b->yoff - 1;
    // 1. prepare above and left
    uint8_t avail_above, avail_left; // indices for dc pred
    const int16_t *above;
    const int16_t *left;
    int16_t *above_tmp = NULL, *left_tmp = NULL; // for cv qualifirer issues
    if (border_y < 0) {
        avail_above = 0;
        above = c1pd__128_border + 1;
    } else {
        avail_above = 1;
        const int delta = border_x; // delta>=-1
        above_tmp = c1_mpool_alloc_def((bh + bw) * sizeof(int16_t));
        // border at least bh+bw offs -1, ozwis fill 128 at inval pos
        if (delta < 0)
            above_tmp[0] = 128;
        for (int j = delta < 0 ? -delta : 0; j < bh + bw; j++) {
            if (j + delta >= pix->w) {
                for (; j < bh + bw; j++)
                    above_tmp[j] = 128;
                break;
            }
            above_tmp[j] = *c1_pixbuf_geti16c(pix, border_y, j + delta);
        }
        above = above_tmp + 1;
    }
    if (border_x < 0) {
        avail_left = 0;
        left = c1pd__128_border + 1;
    } else {
        avail_left = 1;
        const int delta = border_y;
        left_tmp = c1_mpool_alloc_def((bh + bw) * sizeof(int16_t));
        if (delta < 0)
            left_tmp[0] = 128;
        for (int i = delta < 0 ? -delta : 0; i < bh + bw; i++) {
            if (i + delta >= pix->h) {
                for (; i < bh + bw; i++)
                    left_tmp[i] = 128;
                break;
            }
            left_tmp[i] = *c1_pixbuf_geti16c(pix, i + border_y, border_x);
        }
        left = left_tmp + 1;
    }

    // gather pix if src is used in prediction (currently only paeth mode)
    int16_t *input = NULL;
    if (opt->mode == C1_PRED_PAETH) {
        input = c1_mpool_alloc_def(bh * bw * sizeof(int16_t));
        for (uint8_t i = 0; i < bh; i++) {
            for (uint8_t j = 0; j < bw; j++) {
                input[i * bw + j] = *c1_pixbuf_geti16c(pix, border_y + 1 + (int)i, border_x + 1 + (int)j);
            }
        }
    }
    // conduct pred, special case for dc mode
    c1pd_intra_func_t pred_func;
    if (opt->mode == C1_PRED_DC) {
        pred_func = c1pd_dc_preds[avail_left][avail_above][b->size];
    } else {
        pred_func = c1pd_intra_preds[opt->mode - C1_PRED_DC - 1][b->size];
    }
    pred_func(input, output, above, left);

    // destruction

    if (input) {
        c1_mpool_dealloc_def(bh * bw * sizeof(int16_t), input);
    }
    if (above_tmp) {
        c1_mpool_dealloc_def((bh + bw) * sizeof(int16_t), above_tmp);
    }
    if (left_tmp) {
        c1_mpool_dealloc_def((bh + bw) * sizeof(int16_t), left_tmp);
    }
    return 0;
}

// TODO in case not newmv, the mv is a delta to candidate mv gathered? FUCK
static int c1pd__predict_inter(const c1enc_block_t *b, int16_t *output, //
                               const c1_pixbuf_t *pix, const c1pd_option_t *opt) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;
    const int mv_miny = -by, mv_minx = -bx;
    const int mv_maxy = pix->h - by - bh, mv_maxx = pix->w - bx - bw;
    const int mv_y = c1pd__clamp(opt->mv.y, mv_miny, mv_maxy);
    const int mv_x = c1pd__clamp(opt->mv.x, mv_minx, mv_maxx);
    for (int i = 0; i < bh; i++) {
        for (int j = 0; j < bw; j++) {
            output[i * bw + j] = *c1_pixbuf_geti16c(pix, by + i + mv_y, bx + j + mv_x);
        }
    }
    return 0;
}

static int c1pd__predict_cfl(const c1enc_block_t *b, int16_t *output,            //
                             const c1_pixbuf_t *pix, const c1_pixbuf_t *pix_ref, //
                             int8_t *cfl_alpha, uint8_t has_cfl_alpha) {
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;

    // 1. calc cfl_alpha if required
    if (!has_cfl_alpha) {

        // regr y = alpha*x+dc
        int32_t sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int i = 0; i < bh; i++) {
            for (int j = 0; j < bw; j++) {
                // x is luma and y is chroma. 16 bit sufficient
                int16_t x = *c1_pixbuf_geti16c(pix_ref, by + i, bx + j);
                int16_t y = *c1_pixbuf_geti16c(pix, by + i, bx + j);
                sx += x, sy += y, sxx += x * x, sxy += x * y;
            }
        }
        // calculate clamp(divident/divisor, -16, 16)
        // int32_t is enought: n*sxx < 64*64*255*255 < 2**28
        int32_t divident = bh * bw * sxy - sx * sy;
        int32_t divisor = (bh * bw * sxx - sx * sx) / 64; // alpha is scaled up in 64
        if (divisor < 0)
            divisor = -divisor, divident = -divident;
        divident += divisor / 2; // rounding
        // speed up division?
        if (divisor == 0) {
            *cfl_alpha = divident < 0 ? -16 : 16;
        } else if (divident < -16 * divisor)
            *cfl_alpha = -16;
        else if (divident > 16 * divisor) {
            *cfl_alpha = 16;
        } else {
            *cfl_alpha = (int8_t)c1pd__clamp(divident / divisor, -16, 16);
        }
    }

    // 2. derive implicit dc
    int16_t dc = 0, ndc = 0;
    if (bx > 0) {
        for (int i = 0; i < bh && by + i < pix->h; i++) {
            dc += *c1_pixbuf_geti16c(pix, by + i, bx - 1), ndc++;
        }
    }
    if (by > 0) {
        for (int j = 0; j < bw && bx + j < pix->w; j++) {
            dc += *c1_pixbuf_geti16c(pix, by - 1, bx + j), ndc++;
        }
    }
    dc = ndc ? dc / ndc : 128;

    // 3. conduct pred

    for (int i = 0; i < bh; i++) {
        for (int j = 0; j < bw; j++) {
            int16_t x = *c1_pixbuf_geti16c(pix_ref, by + i, bx + j);
            output[i * bw + j] = dc + C1_ROUND_MID(x * (*cfl_alpha), 64);
        }
    }
    return 0;
}

int c1pd_predict(const c1enc_block_t *b, int16_t *output, //
                 const c1_pixbuf_t *pix, const c1pd_option_t *opt, int8_t *cfl_alpha) {
    int r = 0;
    c1_pixbuf_t ci_pix = c1_pixbuf_fromchnl(pix, opt->ci);
    if (opt->use_cfl && opt->ci > 0) {
        c1_pixbuf_t ref_pix = c1_pixbuf_fromchnl(pix, 0);
        r = c1pd__predict_cfl(b, output, &ci_pix, &ref_pix, cfl_alpha, opt->has_cfl_alpha);
        c1_pixbuf_clear(&ref_pix);
    } else if (c1_pred_is_inter(opt->mode)) {
        r = c1pd__predict_inter(b, output, &ci_pix, opt);
    } else if (c1_pred_is_intra(opt->mode)) {
        r = c1pd__predict_intra(b, output, &ci_pix, opt);
    } else {
        warning2("invalid pred mode %d", opt->mode);
        r = -1;
    }
dtor:
    c1_pixbuf_clear(&ci_pix);
    return r;
}

int c1pd_reconstruct(const c1enc_block_t *b, c1enc_frame_t *frm, const c1enc_ctx_t *ctx) {
    assert_fatal(b->pred_type_determined);
    const int bw = c1_sz2wid(b->size), bh = c1_sz2hgt(b->size);
    const int by = (int)b->sb_y * 64 + b->yoff;
    const int bx = (int)b->sb_x * 64 + b->xoff;

    // (1) prepare temp output
    int16_t *pred_output = c1_mpool_alloc_def(bh * bw * sizeof(int16_t));
    assert_fatal(pred_output);

    // (2) translate predict option from mode info

    c1pd_option_t opts[3] = {0};
    int8_t cfl_alphas[3] = {0};
    const c1_pixbuf_t *ref_pix = NULL;

    for (uint8_t ci = 0; ci < 3; ci++)
        opts[ci].ci = ci;

    if (b->pred_type == C1_PRED_INTRA) {
        assert_fatal(b->intra_cand_cnt > 0);
        const c1enc_mi_intra_t *mi = b->intra_cands + 0;
        ref_pix = &frm->pix;

        for (int ci = 0; ci < 3; ci++) {
            opts[ci].use_cfl = mi->use_cfl;
            opts[ci].has_cfl_alpha = 0;
        }
        if (mi->use_cfl) {
            cfl_alphas[1] = mi->cfl_alpha_u;
            cfl_alphas[2] = mi->cfl_alpha_v;
        }
        opts[0].mode = mi->mode_y;
        opts[1].mode = mi->mode_uv;
        opts[2].mode = mi->mode_uv;

    } else if (b->pred_type == C1_PRED_INTER) {
        assert_fatal(b->inter_cand_cnt > 0);
        const c1enc_mi_inter_t *mi = b->inter_cands + 0;
        ref_pix = c1enc_ctx_frame_at(ctx, &frm->pix, mi->ref_frame);

        if (mi->mode != C1_PRED_MVNEW) {
            fatal("unimpl");
        }
        for (int ci = 0; ci < 3; ci++) {
            opts[ci].mode = mi->mode;
            opts[ci].mv = mi->mv;
        }

    } else {
        fatal("unknown pred type %u", b->pred_type);
    }
    assert_fatal(ref_pix);

    // (3) predict and add up residuals

    for (int ci = 0; ci < 3; ci++) {
        c1pd_predict(b, pred_output, ref_pix, opts + ci, cfl_alphas + ci);
        for (int i = 0; i < bh; i++) {
            for (int j = 0; j < bw; j++) {
                *c1_pixbuf_geti16(&frm->pix, by + i, bx + j) = pred_output[i * bw + j] + b->p[ci].diff[i * bw + j];
            }
        }
    }


    // (4) dealloc
    c1_mpool_dealloc_def(bh*bw*sizeof(int16_t),pred_output);

    return 0;
}