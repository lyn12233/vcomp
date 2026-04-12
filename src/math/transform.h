#ifndef C1_MATH_TX_H
#define C1_MATH_TX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "src/util/pixbuf.h"

#include <stdint.h>

/* the input to a transform procedural is transform block size (2D) and transform type (2D).
 while transform types of col and row are induced further. the caller should provide
 initialized pixel buffer to store the result. other implementations (e.g. simd) may be introduced.
 for future compatibility, input is stored again in a compact mem.
*/

#define C1_TX1_SIZE_CNT 4 // 8,16,32,64
#define C1_TX2_SIZE_CNT 4 // 4x4 ... 64x64
#define C1_TX1_TYPE_CNT 7
#define C1_TX2_TYPE_CNT 4

// 1d tx function type
typedef void (*c1tx_func_t)(const int32_t *input, int32_t *output, int8_t cos_bit);
extern c1tx_func_t c1tx_func_array[C1_TX1_TYPE_CNT];
extern c1tx_func_t c1tx_inv_func_array[C1_TX1_TYPE_CNT];

extern const int8_t c1tx_cos_bit_col[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
extern const int8_t c1tx_cos_bit_row[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
extern const int8_t c1tx_inv_cos_bit;
extern const int8_t c1tx_shift_ls[C1_TX2_SIZE_CNT][3];
extern const int8_t c1tx_inv_shift_ls[C1_TX2_SIZE_CNT][2];

extern const int32_t c1tx_cospi_arr_data[4][64];
extern const int32_t c1tx_sinpi_arr_data[4][5];
extern const int c1tx_cos_bit_min; // 10

enum {
    TX2SZ_8_8,
    TX2SZ_16_16,
    TX2SZ_32_32,
    TX2SZ_64_64,
};
typedef uint8_t c1_tx2d_size_t;

enum {
    TX1TYPE_DCT_8,
    TX1TYPE_DCT_16,
    TX1TYPE_DCT_32,
    TX1TYPE_DCT_64,
    TX1TYPE_IDEN_8,
    TX1TYPE_IDEN_16,
    TX1TYPE_IDEN_32,
    // TXTYPE_ADST_8,
    // TXTYPE_ADST_16,
};
typedef uint8_t c1_tx1d_type_t;

enum {
    TX2TYPE_DCT_DCT,
    TX2TYPE_H_DCT,
    TX2TYPE_V_DCT,
    TX2TYPE_IDEN,
    // TXTYPE_ADST_ADST,
    // TXTYPE_H_ADST,
    // TXTYPE_V_ADST,
};
typedef uint8_t c1_tx2d_type_t;

struct c1tx_option_s {
    c1_tx2d_size_t txsize;
    c1_tx2d_type_t txtype;
    // these options are determined from the former ones.
    // they are used for both forward and inverse transform
    uint8_t cos_bit_col, cos_bit_row;
    uint8_t txtype_col, txtype_row;
    uint8_t txsize_col, txsize_row;
    uint8_t flip_col, flip_row;
};
typedef struct c1tx_option_s c1tx_option_t;

int c1tx_extend_option(c1tx_option_t *opt);

// round utils
int32_t c1tx_round_shift(int64_t value, int bit);
void c1tx_round_shift_array(int32_t *arr, int size, int bit);

/** perform forward transform based on option struct
 note: (1) input and output can be the same
 @param buf caller should provide temporary storage of middle result.
 should be a compact space of int32_t[txh*txw]. this provides an option not to alloc on stack
 @param opt transform option, now only needs 2d transform type and size. other fields are extended
*/
int c1tx_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf);
int c1tx_inv_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf);

#ifdef __cplusplus
}
#endif
#endif
