#ifndef C1_ENCODE_INTRA_PRED_H
#define C1_ENCODE_INTRA_PRED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "util/pixbuf.h"

#include <stdint.h>

#define C1_PD_INTRA_CNT 10
#define C1_PD_INTER_CNT 3

/** intra predictor function type. for potential opt purposes.
 the param input are original pixels and output are prediction.
 they are compact int16_t single color channel data gathered by callers.
 for some predictor types, input, above and left are placeholders and may not be used.
 either above or left should point to offset of 1 of a array of at least h+w values
*/
typedef void (*c1pd_intra_func_t)(const int16_t *input, int16_t *output, //
                                  const int16_t *above, const int16_t *left);

/** overriable function tables with constant type and size parameters.
 for prediction and reconstruction.
 dc functions are extracted to a specific table, since dc pred relies on the existence of above and left.
 intra_pred table indices: <prediction-type> <2d-size>
 dc_pred table indices: <has-left> <has-above> <2d-size>
*/
extern c1pd_intra_func_t c1pd_intra_preds[C1_PD_INTRA_CNT - 1][C1_SIZE_CNT];
extern c1pd_intra_func_t c1pd_dc_preds[2][2][C1_SIZE_CNT];
extern c1pd_intra_func_t c1pd_intra_recons[C1_PD_INTRA_CNT - 1][C1_SIZE_CNT];
extern c1pd_intra_func_t c1pd_dc_recons[2][2][C1_SIZE_CNT];

typedef struct {
    // C1_2D_SZ size; unused. this is block size
    C1_PRED_MODE mode;
    uint8_t ci; // color idx: yuv
    uint8_t use_cfl;
    uint8_t has_cfl_alpha;
    c1enc_mv_t mv;
} c1pd_option_t;

// some utils to init pred option?
// todo re-do things below
/** perform either intra or inter prediction based on option struct.
 this is a mid layer between encoder and custom predictor impls.
 (1) no subsampling is considered
 @param[in] b block containing block offset info
 @param[out] output compact space to store pred result, at the size of b. in most cases is b->p[ci].diff, but in case
 ...
 @param[in] pix format c3i16; case intra, pixbuf of current frame; case inter, pixbuf of reference frame
 @param[in] opt predict option
 @param[out] cfl_alpha for option if ci>0 and use_cfl, mode is neglected and this pointer is required ozrwis unused.
 cfl_alpha is represented in int8_t range (-16/64,16/64)
*/
int c1pd_predict(const c1enc_block_t *b, int16_t *output, //
                 const c1_pixbuf_t *pix, const c1pd_option_t *opt, int8_t *cfl);

/** perform prediction reconstruction for block.
 this function calls c1pd_predict for a inverse prediction step, then adds (dequantized) residuals.
 @param[in] b block containing (1) reconstructed residuals in b->p[ci].diff; (2) intra/inter decision and a best mi,
 which is used to derive pred opt.
 @param[in,out] frm recons frame, its pix is to be referred and reconstructed.
 @param[in] ctx context, to determine ref frame.
*/
int c1pd_reconstruct(const c1enc_block_t *b, c1enc_frame_t *frm, const c1enc_ctx_t *ctx);
int c1pd_reconstruct_sb(const c1enc_super_block_t *sb, c1enc_frame_t *frm, const c1enc_ctx_t *ctx);

// --- helper func ---

static const char *c1pd_mode2str(C1_PRED_MODE mode) {
    switch (mode) {
    case C1_PRED_MVNEW:
        return "MV_NEW";
    case C1_PRED_MVNEAR:
        return "MV_NEAR";
    case C1_PRED_MVNEAREST:
        return "MV_NEAREST";
    case C1_PRED_DC:
        return "DC";
    case C1_PRED_H:
        return "H";
    case C1_PRED_V:
        return "V";
    case C1_PRED_D45:
        return "D45";
    case C1_PRED_D135:
        return "D135";
    case C1_PRED_D67:
        return "D67";
    case C1_PRED_D113:
        return "D113";
    case C1_PRED_D157:
        return "D157";
    case C1_PRED_D203:
        return "D203";
    case C1_PRED_PAETH:
        return "PAETH";
    default:
        return "?";
    }
}

#ifdef __cplusplus
}
#endif
#endif