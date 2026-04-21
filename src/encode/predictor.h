#ifndef C1_ENCODE_INTRA_PRED_H
#define C1_ENCODE_INTRA_PRED_H
#include "types.h"
#include "util/pixbuf.h"
#ifdef __cplusplus
extern "C" {
#endif

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
    C1_2D_SZ size;
    C1_PRED_MODE mode;
    // needs some frame context!
    uint8_t ci; // color idx: yuv
    uint16_t yoffs, xoffs;
    const c1_pixbuf_t *frmpix; // all frame pixels possibly in frame_t, c3i16
} c1pd_option_t;

// some utils to init pred option?
// todo re-do things below
/** perform either intra or inter prediction based on option struct.
 this is a mid layer between encoder and custom predictor impls.
 @param[in] input c1i16
 @param[out] output c1i16
*/
int c1pd_predict(c1_pixbuf_t *input, c1_pixbuf_t *output, c1pd_option_t *opt);
/** perform inter/intra reconstruction
 */
int c1pd_reconstruct(c1_pixbuf_t *input, c1_pixbuf_t *output, c1pd_option_t *opt);

#ifdef __cplusplus
}
#endif
#endif