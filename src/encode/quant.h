/** @file quant.h
 (1) quantization steps: q->mult+shift->qcoef=coef*mult/2**shift
 (2) dequantization steps: dqcoef=qcoef*q
 (3) quantized coefficients encoding:
    - default NUM_BASE_LEVELS=2, COEF_BASE_RANGE=12, BR_SIZE=3
    - qcoef<=NUM_BASE_LEVELS: coef_base=qcoef
    - qcoef>NUM_BASE_LEVELS: coef_base=NUM_BASE_LEVELS+1,
        - multiple br(base range)'s [BR_SIZE,BR_SIZE,...,(<BR_SIZE)]
    - qcoef>NUM_BASE_LEVELS+COEF_BASE_RANGE: golomb_len_bits+golomb_data_bits
 (4) bit rate estimation: NOT IMPL currently only look at eob to compare txfms
*/
#ifndef C1_ENCODE_QUANT_H
#define C1_ENCODE_QUANT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include <stdint.h>

// --- a little bit of tx utils ---
// currently not organized into a single file for simplicity

enum {
    C1TX_MEASURE_NOP,
    C1TX_MEASURE_RATE,
};
typedef uint8_t C1_TX_MEASURE_TYPE;

typedef struct {
    uint8_t size_depth;       // searches tx size in block's size..size - size_depth
    C1_TX_2D_TYPE tx_rng_max; // DCT_DCT..rng_max-1
    C1_TX_MEASURE_TYPE measure;
    uint16_t qstep;
} c1tx_search_option_t;

int c1tx_search_sb(c1enc_super_block_t *sb, c1tx_search_option_t opt);

// --- quant ---

typedef struct {
    uint16_t shift, mult, qstep;
} c1_quant_t;

extern uint8_t c1_lookup_q_inf_inited;
extern c1_quant_t c1_lookup_q_dc_inf[3][256];
extern c1_quant_t c1_lookup_q_ac_inf[3][256];

void c1_lookup_init_q_inf();
static c1_quant_t c1q_get_q_inf(uint8_t ac, uint8_t plane, uint8_t idx) {
    if (!c1_lookup_q_inf_inited) {
        c1_lookup_init_q_inf();
        c1_lookup_q_inf_inited = 1;
    }
    return ac ? c1_lookup_q_ac_inf[plane][idx] : c1_lookup_q_dc_inf[plane][idx];
}

/** gather q index at frame level.
 in libaom, the q index is determined by global matrices and history qi's. however this empirical model is complex to
 adapt. thus currently this func traverse super blocks gathering qi by a simple but fixed qp controlled model. the frame
 base qi is takes the average and the superblock delta qi is calculated, qi clampped.

 @param frm frame instance
 @param qp quantization parameter, may be a part of a larger parm group
*/
int c1enc_frame_gather_qi(c1enc_frame_t *frm, uint8_t qp);
/** gather i index at superblock level.
 
 currently impl 2 fixed qp control model:
    - qstep/2 approx mean-3*qp*std
    - ? todo
 after qstep is given, qi is bisect'ed from a lookup table defiend in av1.
 */
int c1enc_sb_gather_qi(c1enc_super_block_t *sb, uint8_t qp);

int c1enc_quantize_sb(c1enc_super_block_t *sb);

#ifdef __cplusplus
}
#endif
#endif