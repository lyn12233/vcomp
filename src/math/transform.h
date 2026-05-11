#ifndef C1_MATH_TX_H
#define C1_MATH_TX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "encode/types.h"
#include "util/pixbuf.h"

#include <stdint.h>

/* the input to a transform procedural is transform block size (2D) and transform type (2D).
 while transform types of col and row are induced further. the caller should provide
 initialized pixel buffer to store the result. other implementations (e.g. simd) may be introduced.
 for future compatibility, input is stored again in a compact mem.
*/

#define C1_TX1_SIZE_CNT 4           // 8,16,32,64
#define C1_TX2_SIZE_CNT C1_SIZE_CNT // 8x8 ... 64x64
#define C1_TX1_TYPE_CNT 7
#define C1_TX2_TYPE_CNT C1_TX_TYPE_CNT

/** 1d tx function type.
 input and output are non identical buffers storing compact data.
 potential opt purposes.
 caller to these functions should prepare them, possibly on stack.
 */
typedef void (*c1tx_func_t)(const int32_t *input, int32_t *output, int8_t cos_bit);

// overridable func array for 1d tx and global lookup tables
// potential opt is at 1d tx

extern c1tx_func_t c1tx_func_array[C1_TX1_TYPE_CNT];
extern c1tx_func_t c1tx_inv_func_array[C1_TX1_TYPE_CNT];

extern const uint8_t c1tx_cos_bit_col[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
extern const uint8_t c1tx_cos_bit_row[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
static const uint8_t c1tx_inv_cos_bit = 12;
extern const int8_t c1tx_shift_ls[C1_TX2_SIZE_CNT][3];
extern const int8_t c1tx_inv_shift_ls[C1_TX2_SIZE_CNT][2];

extern const int32_t c1tx_cospi_arr_data[4][64];
extern const int32_t c1tx_sinpi_arr_data[4][5];
static const int c1tx_cos_bit_min = 10;

// transform option

struct c1tx_option_s {
    C1_2D_SZ txsize;      // 2d size enum
    C1_TX_2D_TYPE txtype; // tranform type
    // these options are determined from the former ones.
    // they are used for both forward and inverse transform
    uint8_t cos_bit_col, cos_bit_row;
    uint8_t txtype_col, txtype_row;
    uint8_t txsize_col, txsize_row; // col/row size in pixels, e.g. col size is height
    uint8_t flip_col, flip_row;     // whether use flip, currently unused
};
typedef struct c1tx_option_s c1tx_option_t;

int c1tx_extend_option(c1tx_option_t *opt);

// round utils
int32_t c1tx_round_shift(int64_t value, int bit);
void c1tx_round_shift_array(int32_t *arr, int size, int bit);

/** perform 2D forward transform based on option struct
 @param input input pixel buffer, c1i16, compact
 @param output output pixel buffer, c1i32, compact
 @param opt transform option, now only needs 2d transform type and size. other fields are extended
*/
int c1tx_txfm2d(const int16_t *input, int32_t *output, c1tx_option_t *opt);
/** perform 2D inerse transform.
 @param[in] input c1i32
 @param[out] output c1i16
 */
int c1tx_inv_txfm2d(const int32_t *input, int16_t *output, c1tx_option_t *opt);

#ifdef __cplusplus
}
#endif
#endif
