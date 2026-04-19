#ifndef C1_MATH_TX_H
#define C1_MATH_TX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "src/encode/types.h"
#include "src/util/pixbuf.h"

#include <stdint.h>

/* the input to a transform procedural is transform block size (2D) and transform type (2D).
 while transform types of col and row are induced further. the caller should provide
 initialized pixel buffer to store the result. other implementations (e.g. simd) may be introduced.
 for future compatibility, input is stored again in a compact mem.
*/

#define C1_TX1_SIZE_CNT 4 // 8,16,32,64
#define C1_TX2_SIZE_CNT 4 // 8x8 ... 64x64
#define C1_TX1_TYPE_CNT 7
#define C1_TX2_TYPE_CNT 4

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

extern const int8_t c1tx_cos_bit_col[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
extern const int8_t c1tx_cos_bit_row[C1_TX1_SIZE_CNT][C1_TX1_SIZE_CNT];
static const int8_t c1tx_inv_cos_bit = 12;
extern const int8_t c1tx_shift_ls[C1_TX2_SIZE_CNT][3];
extern const int8_t c1tx_inv_shift_ls[C1_TX2_SIZE_CNT][2];

extern const int32_t c1tx_cospi_arr_data[4][64];
extern const int32_t c1tx_sinpi_arr_data[4][5];
static const int c1tx_cos_bit_min = 10;

// transform option

struct c1tx_option_s {
    C1_2D_SZ txsize;
    C1_TX_2D_TYPE txtype;
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

/** perform 2D forward transform based on option struct
 note: (1) input and output can be the same
 @param input input pixel buffer, color format c1i16
 @param output output pixel buffer, should be at least the size of input, c1i32
 @param buf caller should provide temporary storage of middle result.
 should be a compact space of int32_t[txh*txw]. this provides an option not to alloc on stack
 @param opt transform option, now only needs 2d transform type and size. other fields are extended
*/
int c1tx_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf);
/** perform 2D inerse transform
 @param input color format c1i32
 @param output color format c1i16
 */
int c1tx_inv_txfm2d(c1_pixbuf_t *input, c1_pixbuf_t *output, c1tx_option_t *opt, int32_t *buf);

#ifdef __cplusplus
}
#endif
#endif
