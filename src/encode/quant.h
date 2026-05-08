/** @file quant.h
 (1) quantization steps: q->mult+shift->qcoef=coef*mult/2**shift
 (2) dequantization steps: dqcoef=qcoef*q
 (3) quantized coefficients encoding:
    - default NUM_BASE_LEVELS=2, COEF_BASE_RANGE=12, BR_SIZE=3
    - qcoef<=NUM_BASE_LEVELS: coef_base=qcoef
    - qcoef>NUM_BASE_LEVELS: coef_base=NUM_BASE_LEVELS+1,
        - multiple br(base range)'s [BR_SIZE,BR_SIZE,...,(<BR_SIZE)]
    - qcoef>NUM_BASE_LEVELS+COEF_BASE_RANGE: golomb_len_bits+golomb_data_bits
 (4) bit rate estimation: ?
    - based on default tx: dct-dct
    - extract coef dist to a hist
    - cost of each syntaxes is the entropy given est cdf, not considering pixel's ctx
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

int c1tx_search_b(c1enc_block_t *b, c1tx_search_option_t opt);
static int c1tx_search_part(c1enc_partition_t *p, c1tx_search_option_t opt) {
    int r = 0;
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            if ((r = c1tx_search_part(p->parts[i], opt)) < 0)
                return r;
        }
    } else {
        if ((r = c1tx_search_b(p->b, opt)) < 0)
            return r;
    }
    return r;
}

// --- quant ---

typedef struct {
    uint16_t shift, mult;
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

int c1enc_frame_gather_qi(c1enc_frame_t *frm, uint8_t qp);
int c1enc_sb_gather_qi(c1enc_super_block_t *sb, uint8_t qp);

#ifdef __cplusplus
}
#endif
#endif