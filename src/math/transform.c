#include "transform.h"
#include "src/util/log.h"

#include <limits.h>
#include <stdint.h>

// get column 1d tranxform type, silent invalid combinations, like 64x64 ident
static c1_tx1d_type_t c1tx__get_col_type(c1_tx2d_size_t sz, c1_tx2d_type_t tp) {
    static const c1_tx1d_type_t lookup[C1_TX2_SIZE_CNT][C1_TX2_TYPE_CNT] = {
        {TX1TYPE_DCT_8, TX1TYPE_IDEN_8, TX1TYPE_DCT_8, TX1TYPE_IDEN_8},
        {TX1TYPE_DCT_16, TX1TYPE_IDEN_16, TX1TYPE_DCT_16, TX1TYPE_IDEN_16},
        {TX1TYPE_DCT_32, TX1TYPE_IDEN_32, TX1TYPE_DCT_32, TX1TYPE_IDEN_32},
        {TX1TYPE_DCT_64, TX1TYPE_DCT_64, TX1TYPE_DCT_64, TX1TYPE_DCT_64},
    };
    return lookup[sz][tp];
}

// get row 1d tx type, see above
static c1_tx1d_type_t c1tx__get_row_type(c1_tx2d_size_t sz, c1_tx2d_type_t tp) {
    static const c1_tx1d_type_t lookup[C1_TX2_SIZE_CNT][C1_TX2_TYPE_CNT] = {
        {TX1TYPE_DCT_8, TX1TYPE_DCT_8, TX1TYPE_IDEN_8, TX1TYPE_IDEN_8},
        {TX1TYPE_DCT_16, TX1TYPE_DCT_16, TX1TYPE_IDEN_16, TX1TYPE_IDEN_16},
        {TX1TYPE_DCT_32, TX1TYPE_DCT_32, TX1TYPE_IDEN_32, TX1TYPE_IDEN_32},
        {TX1TYPE_DCT_64, TX1TYPE_DCT_64, TX1TYPE_DCT_64, TX1TYPE_DCT_64},
    };
    return lookup[sz][tp];
}

static uint8_t c1tx__get_col_sz(c1_tx2d_size_t sz) {
    static const uint8_t lookup[C1_TX2_SIZE_CNT] = {8, 16, 32, 64};
    return lookup[sz];
}
static uint8_t c1tx__get_row_sz(c1_tx2d_size_t sz) {
    static const uint8_t lookup[C1_TX2_SIZE_CNT] = {8, 16, 32, 64};
    return lookup[sz];
}
static uint8_t c1tx__get_col_sz_idx(c1_tx2d_size_t sz) {
    static const uint8_t lookup[C1_TX2_SIZE_CNT] = {0, 1, 2, 3};
    return lookup[sz];
}
static uint8_t c1tx__get_row_sz_idx(c1_tx2d_size_t sz) {
    static const uint8_t lookup[C1_TX2_SIZE_CNT] = {0, 1, 2, 3};
    return lookup[sz];
}
static int64_t c1tx__clamp64(int64_t x, int64_t lb, int64_t ub) {
    x = x < lb ? lb : x;
    x = x > ub ? ub : x;
    return x;
}

static void c1tx__dct_8(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__dct_16(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__dct_32(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__dct_64(const int32_t *input, int32_t *output, int8_t cos_bit);

static void c1tx__iden_8(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__iden_16(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__iden_32(const int32_t *input, int32_t *output, int8_t cos_bit);

static void c1tx__idct_8(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__idct_16(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__idct_32(const int32_t *input, int32_t *output, int8_t cos_bit);
static void c1tx__idct_64(const int32_t *input, int32_t *output, int8_t cos_bit);

c1tx_func_t c1tx_func_array[C1_TX1_TYPE_CNT] = {             //
    c1tx__dct_8,  c1tx__dct_16,  c1tx__dct_32, c1tx__dct_64, //
    c1tx__iden_8, c1tx__iden_16, c1tx__iden_32};
c1tx_func_t c1tx_inv_func_array[C1_TX1_TYPE_CNT] = {           //
    c1tx__idct_8, c1tx__idct_16, c1tx__idct_32, c1tx__idct_64, //
    c1tx__iden_8, c1tx__iden_16, c1tx__iden_32};

// 1D size - cos bit mapping. see [libaom]/encoder/av1_fwd_txfm2d.c
const int8_t c1tx_cos_bit_col[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT] = {
    {13, 0, 0, 0},
    {0, 13, 0, 0},
    {0, 0, 12, 0},
    {0, 0, 0, 13},
};
const int8_t c1tx_cos_bit_row[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT] = {
    {13, 0, 0, 0},
    {0, 12, 0, 0},
    {0, 0, 12, 0},
    {0, 0, 0, 10},
};
const int8_t c1tx_inv_cos_bit = 12;

// how much to shift bits left at the intervals of col/row tx
// suffices 2**total_shift_left = 2/sqrt(nbcol*nbrow), the dct coef
const int8_t c1tx_shift_ls[C1_TX2_SIZE_CNT][3] = {
    {2, -1, 0},  // 8x8
    {2, -2, 0},  // 16x16
    {2, -4, 0},  // 32x32
    {0, -2, -2}, // 64x64
};
const int8_t c1tx_inv_shift_ls[C1_TX2_SIZE_CNT][2] = {
    {-1, -4},
    {-2, -4},
    {-2, -4},
    {-2, -4},
};

// <cos_bit=10,..13><theta=pi/128*0..63> -> cos theta in cos-bit precision
// clang-format off
const int32_t c1tx_cospi_arr_data[4][64] = {
  { 1024, 1024, 1023, 1021, 1019, 1016, 1013, 1009, 1004, 999, 993, 987, 980,
    972,  964,  955,  946,  936,  926,  915,  903,  891,  878, 865, 851, 837,
    822,  807,  792,  775,  759,  742,  724,  706,  688,  669, 650, 630, 610,
    590,  569,  548,  526,  505,  483,  460,  438,  415,  392, 369, 345, 321,
    297,  273,  249,  224,  200,  175,  150,  125,  100,  75,  50,  25 },
  { 2048, 2047, 2046, 2042, 2038, 2033, 2026, 2018, 2009, 1998, 1987,
    1974, 1960, 1945, 1928, 1911, 1892, 1872, 1851, 1829, 1806, 1782,
    1757, 1730, 1703, 1674, 1645, 1615, 1583, 1551, 1517, 1483, 1448,
    1412, 1375, 1338, 1299, 1260, 1220, 1179, 1138, 1096, 1053, 1009,
    965,  921,  876,  830,  784,  737,  690,  642,  595,  546,  498,
    449,  400,  350,  301,  251,  201,  151,  100,  50 },
  { 4096, 4095, 4091, 4085, 4076, 4065, 4052, 4036, 4017, 3996, 3973,
    3948, 3920, 3889, 3857, 3822, 3784, 3745, 3703, 3659, 3612, 3564,
    3513, 3461, 3406, 3349, 3290, 3229, 3166, 3102, 3035, 2967, 2896,
    2824, 2751, 2675, 2598, 2520, 2440, 2359, 2276, 2191, 2106, 2019,
    1931, 1842, 1751, 1660, 1567, 1474, 1380, 1285, 1189, 1092, 995,
    897,  799,  700,  601,  501,  401,  301,  201,  101 },
  { 8192, 8190, 8182, 8170, 8153, 8130, 8103, 8071, 8035, 7993, 7946,
    7895, 7839, 7779, 7713, 7643, 7568, 7489, 7405, 7317, 7225, 7128,
    7027, 6921, 6811, 6698, 6580, 6458, 6333, 6203, 6070, 5933, 5793,
    5649, 5501, 5351, 5197, 5040, 4880, 4717, 4551, 4383, 4212, 4038,
    3862, 3683, 3503, 3320, 3135, 2948, 2760, 2570, 2378, 2185, 1990,
    1795, 1598, 1401, 1202, 1003, 803,  603,  402,  201 },
};
const int32_t c1tx_sinpi_arr_data[4][5];
// clang-format on

const int c1tx_cos_bit_min = 10;

int c1tx_extend_option(c1tx_option_t *opt) {
    uint8_t col_idx = c1tx__get_col_sz_idx(opt->txsize);
    uint8_t row_idx = c1tx__get_row_sz_idx(opt->txsize);
    opt->cos_bit_col = c1tx_cos_bit_col[row_idx][col_idx];
    opt->cos_bit_row = c1tx_cos_bit_row[row_idx][col_idx];
    opt->txtype_col = c1tx__get_col_type(opt->txsize, opt->txtype);
    opt->txtype_row = c1tx__get_row_type(opt->txsize, opt->txtype);
    opt->txsize_col = c1tx__get_col_sz(opt->txsize);
    opt->txsize_row = c1tx__get_row_sz(opt->txsize);
    opt->flip_col = 0;
    opt->flip_row = 0;
    return 0;
}

int32_t c1tx_round_shift(int64_t value, int bit) {
    return (int32_t)((value + (1ll << (bit - 1))) >> bit);
}
void c1tx_round_shift_array(int32_t *arr, int size, int bit) {
    if (bit == 0) {
        return;
    } else {
        if (bit > 0) {
            for (int i = 0; i < size; i++) {
                arr[i] = c1tx_round_shift(arr[i], bit);
            }
        } else {
            for (int i = 0; i < size; i++) {
                arr[i] = (int32_t)c1tx__clamp64(((int64_t)1 << (-bit)) * arr[i], INT32_MIN, INT32_MAX);
            }
        }
    }
}
// perfom half of the butterfly unit
static int32_t c1tx__half_btf(int32_t w0, int32_t in0, int32_t w1, int32_t in1, int bit) {
    int64_t result = (int64_t)(w0 * in0) + (int64_t)(w1 * in1);
    result = result + (1ll << (bit - 1));
    return (int32_t)(result >> bit);
}

int c1tx_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf) {
    c1tx_extend_option(opt);
    const c1tx_func_t col_func = c1tx_func_array[opt->txtype_col];
    const c1tx_func_t row_func = c1tx_func_array[opt->txtype_row];
    const int8_t *shift = c1tx_shift_ls[opt->txsize];

    int32_t temp_in[64], temp_out[64];

    // column transform
    for (uint8_t col = 0; col < opt->txsize_col; col++) {
        if (opt->flip_row) { // flip_ud
            fatal2("unimpl");
        } else {
            for (uint8_t row = 0; row < opt->txsize_row; row++) {
                temp_in[row] = *c1_pixbuf_geti32(input, row, col);
            }
        }
        c1tx_round_shift_array(temp_in, opt->txsize_row, -shift[0]);
        col_func(temp_in, temp_out, opt->cos_bit_col);
        c1tx_round_shift_array(temp_out, opt->txsize_row, -shift[1]);
        if (opt->flip_col) { // flip_lr
            fatal2("unimpl");
        } else {
            for (uint8_t row = 0; row < opt->txsize_row; row++) {
                buf[row * opt->txsize_col + col] = temp_out[row];
            }
        }
    }
    // row transform
    for (uint8_t row = 0; row < opt->txsize_row; row++) {
        row_func(buf + row * opt->txsize_col, temp_out, opt->cos_bit_row);
        c1tx_round_shift_array(temp_out, opt->txsize_col, -shift[2]);
        for (uint8_t col = 0; col < opt->txsize_col; col++) {
            *c1_pixbuf_geti32(output, row, col) = temp_out[col];
        }
        // todo: if rect is (a,2a) or (2a,a) mult sqrt 2
    }
    return 0;
}
int c1tx_inv_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf) {
    c1tx_extend_option(opt);
    const c1tx_func_t col_func = c1tx_inv_func_array[opt->txtype_col];
    const c1tx_func_t row_func = c1tx_inv_func_array[opt->txtype_row];
    const int8_t *shift = c1tx_inv_shift_ls[opt->txsize];

    int32_t temp_in[64], temp_out[64];

    // row transform
    for (uint8_t row = 0; row < opt->txsize_row; row++) {
        int32_t *buf_ptr = buf + row * (opt->txsize_col);
        // todo: if rect is (a,2a) or (2a,a) mult sqrt 2
        for (uint8_t col = 0; col < opt->txsize_col; col++) {
            temp_in[col] = *c1_pixbuf_geti32(input, row, col);
        }
        // todo: clamp
        row_func(temp_in, buf_ptr, opt->cos_bit_row);
        c1tx_round_shift_array(buf_ptr, opt->txsize_col, -shift[0]);
    }
    // column transform
    for (uint8_t col = 0; col < opt->txsize_col; col++) {
        if (opt->flip_col) {
            fatal2("unimpl");
        } else {
            for (uint8_t row = 0; row < opt->txsize_row; row++) {
                temp_in[row] = buf[row * opt->txsize_col + col];
            }
        }
        // todo: clamp
        col_func(temp_in, temp_out, opt->cos_bit_col);
        c1tx_round_shift_array(temp_out, opt->txsize_row, -shift[1]);
        if (opt->flip_row) {
            fatal2("unimpl");
        } else {
            for (uint8_t row = 0; row < opt->txsize_row; row++) {
                // suppose bitdepth is 8bit, clamps to 255
                int32_t *out = c1_pixbuf_geti32(output, row, col);
                *out = c1tx__clamp64((int64_t)*out + temp_out[row], 0, 255);
            }
        } // </flip?>
    } // </col_tx>
    return 0;
}

#include "src/math/ref/av1_fwd_txfm1d.c"
#define mkfunc(x, y)                                          \
    static void x(const int32_t *i, int32_t *o, int8_t bit) { \
        y(i, o, bit, NULL);                                   \
    }
// mkfunc(c1tx__dct_8, c1tx__av1_fdct8);
static void c1tx__dct_8(const int32_t* i,int32_t*o, int8_t cos_bit){
    // info("input is (%d, %d, %d, %d, %d, %d, %d, %d)",i[0],i[1],i[2],i[3],i[4],i[5],i[6],i[7]);
    c1tx__av1_fdct8(i,o,cos_bit,NULL);
    // info("output is (%d, %d, %d, %d, %d, %d, %d, %d)",o[0],o[1],o[2],o[3],o[4],o[5],o[6],o[7]);
}
mkfunc(c1tx__dct_16, c1tx__av1_fdct16);
mkfunc(c1tx__dct_32, c1tx__av1_fdct32);
mkfunc(c1tx__dct_64, c1tx__av1_fdct64);
mkfunc(c1tx__iden_8, c1tx__av1_iden8);
mkfunc(c1tx__iden_16, c1tx__av1_iden16);
mkfunc(c1tx__iden_32, c1tx__av1_iden32);
#include "src/math/ref/av1_inv_txfm1d.c"
mkfunc(c1tx__idct_8, c1tx__av1_idct8);
mkfunc(c1tx__idct_16, c1tx__av1_idct16);
mkfunc(c1tx__idct_32, c1tx__av1_idct32);
mkfunc(c1tx__idct_64, c1tx__av1_idct64);