#include "predictor.h"
#include "src/util/log.h"
#include "types.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- utils ---

static void c1pd__memset16(int16_t *output, int16_t val, int nb) {
    for (int i = 0; i < nb; i++)
        output[i] = val;
}
static int16_t c1pd__abs_dif(int16_t a, int16_t b) {
    return a > b ? a - b : b - a;
}

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
        memcpy(output + w * i, above, w);
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
        if (base > base_max) {
            for (int i2 = i; i2 < h; i2++) {
                c1pd__memset16(output + w * i2, above[base_max], w);
            }
            break;
        }
        for (int j = 0; j < w; j++) {
            if (base < base_max) {
                int val = above[base] * (32 - shift) + above[base + 1] * shift;
                output[w * i + j] = C1_ROUND_BITS(val, 5);
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
            output[w * i + j] = C1_ROUND_BITS(val, 5);
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
                output[w * i + j] = C1_ROUND_BITS(val, 5);
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
// first index: has_top
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
