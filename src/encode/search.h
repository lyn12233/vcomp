#ifndef C1_ENCODE_SEARCH_H
#define C1_ENCODE_SEARCH_H
#include "util/pixbuf.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

// --- search utils ---

/** compare rdstat fitness, result>0 means "a" is better.
 considers mactrices' existence, in order fit, dis, rate, sse, sad.
 if a and b do not have overlapping matrices, return 0.
*/
int c1enc_rdstat_cmp(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b);
/** bubble-sort new cand into block's candidate array, maintain its order.
 */
int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat);
int c1enc_block_add_inter_cand(c1enc_block_t *b, const c1enc_mi_inter_t *mi, const c1enc_rdstat_t *stat);

// --- search options ---
// a all-in-one option struct
typedef struct {
    // block level
    // - inter search option
    // - intra search option
    uint8_t intra_try_uv;        // try a different prediction mode for uv channels
    uint8_t intra_try_cfl;       // try chroma-from-luma. try_cfl and try_uv should not be both set
    C1_PRED_MODE intra_rng_max;  // max intra mode (excluded) to search
} c1enc_search_option_t;

/** search intra mode at block level.
 measures by sad.
*/
int c1enc_search_intra_b(c1enc_block_t *b,const c1_pixbuf_t*pix, const c1enc_search_option_t *opt);
int c1enc_search_merge(c1enc_partition_t *part);

#ifdef __cplusplus
}
#endif
#endif