#ifndef C1_ENCODE_SEARCH_H
#define C1_ENCODE_SEARCH_H
#include "encode/types.h"
#include "util/pixbuf.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

// --- search utils ---

/** compare rdstat fitness, result>0 means "a" is better.
 considers mactrices' existence, in order fit, dis, rate, sse, sad.
 @return goodness comparison result, >0 means "a" better, =0 equal, <0 "b" better.
 if a and b do not have overlapping matrices, return 0.
*/
int c1enc_rdstat_cmp(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b);
/** merge rdstat. currently simply adding exisitng fields?
 @return the merged rdstat
 */
c1enc_rdstat_t c1enc_rdstat_merge(const c1enc_rdstat_t *a, const c1enc_rdstat_t *b);
/** check if intra mode info is equal to skip redundant cands?
 */
int c1enc_mi_intra_eq(const c1enc_mi_intra_t *a, const c1enc_mi_intra_t *b);
/** check if inter mode info is equal to skip redundant cands?
 */
int c1enc_mi_inter_eq(const c1enc_mi_inter_t *a, const c1enc_mi_inter_t *b);
/** bubble-sort new intra predict cand into block's candidate array, maintain its order.
 */
int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat);
/** bubble-sort new inter predict cand into block's candidate array, maintain its order.
 */
int c1enc_block_add_inter_cand(c1enc_block_t *b, const c1enc_mi_inter_t *mi, const c1enc_rdstat_t *stat);
/** decide the best prediction mode from intra and inter predict candidates and store in some fields in block_t. if
 * relevant block field is deprecated, remove this.
 */
int c1enc_part_gather_rdstat(c1enc_partition_t *p, int depth);

// --- search options ---
// a all-in-one option struct
typedef struct {
    // block level
    // - inter search option
    uint8_t inter_y0, inter_x0;     // mv search initial mv
    uint8_t inter_init_steps_mask;  // mask 0..6 bit is the step size, 1..64
    uint8_t inter_sad_subsamp_mask; // 2x2 subsample mask, e.g. [msb] 1...10...0. index to this mask is abs(x)+abs(y)
    uint8_t inter_smooth_lambda;    // m = (sad|sad_subsamp) + lambda*(abs(x)+abs(y))/4
    uint8_t inter_newcand_cnt;      // new candidates from search result to add to block's inter cands
    // - intra search option
    uint8_t intra_try_uv;       // try a different prediction mode for uv channels
    uint8_t intra_try_cfl;      // try chroma-from-luma. try_cfl and try_uv should not be both set
    C1_PRED_MODE intra_rng_max; // max intra mode (excluded) to search
} c1enc_search_option_t;

/** search intra mode at block level. for options see c1enc_search_option_t
 measures by abs diff(sad).
 @param b target block
 @param pix c3i16 pixels buf of the current frame.
*/
int c1enc_search_intra_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt);

/** search inter mv at block level. measures abs diff.
 @param b target block
 @param pix c3i16 pixels buf of the current frame.
 */
int c1enc_search_inter_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt);

/** gather adjacent motion vectors
 */

/** try merge to a partition node based on pred search result.
 merge occurs only when: (1) current partition is the last one, relating to 4 blocks. (2) for merge as intra mode, intra
 cands' sads are smooth and none is much worse than inter cands. (3) for merge as inter mode, vice versa. (4) search
 block partition mode at current level, result sad is not much worse than current partition.
 this process adds a pseudo block_t to current partition. case merge is determined, child partitions are cleaned ozrwis
 this block is cleaned.
 @return negative if parms are invalid. a failed merge is not an error.
*/
int c1enc_search_merge(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_search_option_t *opt);

#ifdef __cplusplus
}
#endif
#endif