#ifndef C1_ENCODE_INTRA_PRED_H
#define C1_ENCODE_INTRA_PRED_H
#include "types.h"
#include "util/pixbuf.h"
#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>

#define C1_PD_INTRA_CNT 10

typedef void (*c1pd_intra_func_t)(const int16_t *input, int16_t*output);
extern c1pd_intra_func_t c1pd_intra_preds[C1_PD_INTRA_CNT];
extern c1pd_intra_func_t c1pd_intra_recons[C1_PD_INTRA_CNT];

typedef struct {
    C1_2D_SZ size;
    C1_PRED_MODE mode;
    // needs some frame context!
    uint8_t ci; // color idx: yuv
    uint16_t yoffs,xoffs;
    const c1_pixbuf_t* frmpix; // all frame pixels possibly in frame_t, c3i16 
}c1pd_option_t;

// some utils to init pred option?

/** perform either intra or inter prediction based on option struct.
 this is a mid layer between encoder and custom predictor impls.
 @param[in] input c1i16
 @param[out] output c1i16
*/
int c1pd_predict(c1_pixbuf_t* input, c1_pixbuf_t*output,c1pd_option_t*opt);
/** perform inter/intra reconstruction
*/
int c1pd_reconstruct(c1_pixbuf_t* input, c1_pixbuf_t*output,c1pd_option_t*opt);

#ifdef __cplusplus
}
#endif
#endif