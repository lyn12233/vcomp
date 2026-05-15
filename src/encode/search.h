/** @file search.h
 prediction mode search, measure, block merge/divide strategies
*/
#ifndef C1_ENCODE_SEARCH_H
    #define C1_ENCODE_SEARCH_H
    #ifdef __cplusplus
extern "C" {
    #endif

    #include "encoder.h"
    #include "types.h"
    #include "util/log.h"
    #include "util/pixbuf.h"

// --- --- search utils --- ---

// --- rdstat ops ---

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

// --- mode info ops ---

/** check if intra mode info is equal to skip redundant cands
 @return bool result
 */
int c1enc_mi_intra_eq(const c1enc_mi_intra_t *a, const c1enc_mi_intra_t *b);
/** similar to @ref c1enc_mi_intra_eq */
int c1enc_mi_inter_eq(const c1enc_mi_inter_t *a, const c1enc_mi_inter_t *b);

/** check if the intra cand already exists
 @return bool result
 */
int c1enc_block_has_intra_cand(const c1enc_block_t *b, const c1enc_mi_intra_t *mi);
/** similar to @ref c1enc_block_has_intra_cnad */
int c1enc_block_has_inter_cand(const c1enc_block_t *b, const c1enc_mi_inter_t *mi);

/** bubble-sort new intra predict cand into block's candidate array, maintain its order. */
int c1enc_block_add_intra_cand(c1enc_block_t *b, const c1enc_mi_intra_t *mi, const c1enc_rdstat_t *stat);
/** similar to @ref c1enc_mi_add_intra_cand */
int c1enc_block_add_inter_cand(c1enc_block_t *b, const c1enc_mi_inter_t *mi, const c1enc_rdstat_t *stat);

// --- --- info collection --- ---

/** decide the best prediction mode from intra and inter predict candidates and store in some fields in block_t.
 */
int c1enc_part_gather_rdstat(c1enc_partition_t *p);
/** gather pred type */
int c1enc_block_gather_pred_type(c1enc_block_t *b);
/** gather residual to b->p[ci].diff. this should be called after gather pred_type.
 after this stage diff stores residual rather than pred result.
*/
int c1enc_block_gather_residual(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, uint8_t ref_id);
static int c1enc_part_gather_pred_type(c1enc_partition_t *p) {
    int r;
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            if ((r = c1enc_part_gather_pred_type(p->parts[i])) < 0)
                return r;
        }
        return 0;
    } else {
        return c1enc_block_gather_pred_type(p->b);
    }
}
static int c1enc_part_gather_residual(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx,
                                      uint8_t ref_id) {
    int r;
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            if ((r = c1enc_part_gather_residual(p->parts[i], pix, ctx, ref_id)) < 0)
                return r;
        }
        return 0;
    } else {
        return c1enc_block_gather_residual(p->b, pix, ctx, ref_id);
    }
}

// --- search options ---
// a all-in-one option struct
typedef struct {
    // block level
    // - inter search option
    uint8_t try_inter; // inter searrch switch
    // uint8_t inter_y0, inter_x0;     // mv search initial mv
    uint8_t inter_init_steps_mask;  // mask 0..6 bit is the step size, 1..64
    uint8_t inter_sad_subsamp_mask; // 2x2 subsample mask, e.g. [msb] 1...10...0. index to this mask is abs(x)+abs(y)
    uint8_t inter_smooth_lambda;    // m = (sad|sad_subsamp) + lambda*(abs(x)+abs(y))/4
    uint8_t inter_newcand_cnt;      // new candidates from search result to add to block's inter cands
    uint8_t inter_ref_idx; // index of ref frame in encoder context, note the index in array is avail_frame_cnt-ref_id
    uint8_t inter_only_y;  // only measure the y plane rdstat for inter modes, mult 3 as whole
    // - intra search option
    uint8_t try_intra;          // intra search switch
    uint8_t intra_try_uv;       // try a different prediction mode for uv channels
    uint8_t intra_try_cfl;      // try chroma-from-luma. try_cfl and try_uv should not be both set
    C1_PRED_MODE intra_rng_max; // max intra mode (excluded) to search
    uint8_t intra_only_y;       // only measure the y plane rdstat for intra modes.
    // partition level
    // - merging/dividing option
    // -- to tell if mode(intra/inter) is obviously better than another: loss1<((loss2*mult)>>shift)
    //    this counts for if a merge of specific mode is decided: if intra mode cands are much better than inter cands,
    //    prune further seach on inter cands and vice versa.
    uint8_t thre_mode_better_mult;  // e.g. 3
    uint8_t thre_mode_better_shift; // e.g. 1
    // -- to tell if matrices(SAD) of 4 partitions are not obviously different: min_>=((max_*mult)>>shift)
    //    this counts for if a merge of specific mode is decided: (1) at comparable residual stats, a merged block may
    //    require less encode info overhead. thus merge is considered as soon as the stats of parts are not much better.
    //    (2) if residuals are not distributed uniformly enough, partition may op better with tx and quant.
    uint8_t thre_mat_is_dif_mult;  // e.g. 3
    uint8_t thre_mat_is_dif_shift; // e.g. 2
    // superblock level
    // - palette option
    uint8_t try_palette;
    // misc and large ints
    // - delta for the min_ of matrices for decision (2)
    uint32_t thre_mat_is_dif_delta;
    // - max SAD of a block, may be fixed fraction of frame
    uint32_t thre_sad_max_b;
    uint32_t thre_inter_efficient_sad; // if inter cand has PER PIX sad less than this, skip intra.
    uint32_t thre_skip_inter_sad;      // if first inter per pix sad exceed this, early exit inter search.
    uint32_t thre_intra_efficient_sad; // early exit intra search if current intra mode is efficient enough
} c1enc_search_option_t;

/** option validator
 */
void c1enc_search_option_validate(const c1enc_search_option_t *opt);

// --- pred mode search functions ---

/** search intra mode at block level. for options see c1enc_search_option_t
 measures by abs diff(sad).
 @param b target block
 @param pix c3i16 pixels buf of the current frame.
*/
int c1enc_search_intra_b(c1enc_block_t *b, const c1_pixbuf_t *pix, //
                         const c1enc_search_option_t *opt);

/** search inter mv at block level. measures abs diff.
 @param b target block
 @param pix c3i16 pixels buf of the current frame.
 @param ctx context, to access ref frame pix.
 */
int c1enc_search_inter_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                         const c1enc_search_option_t *opt);
static inline int c1enc_search_b(c1enc_block_t *b, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                                 const c1enc_search_option_t *opt) {
    int r = 0;
    const int bh = c1_sz2hgt(b->size), bw = c1_sz2wid(b->size);
    if (b->inter_cand_cnt > 0 //
        && b->inter_cand_stats[0].sad <= opt->thre_inter_efficient_sad * bh * bw * 3) {
        // debug("inter mode enough(%u), skip intra search", b->inter_cand_stats[0].sad);
        return r;
    }
    // search inter first to skip major overhead in intra search
    // also skip this case scene change(inter not acceptable).
    if (opt->try_inter //
        && (b->inter_cand_cnt == 0 || b->inter_cand_stats[0].sad <= opt->thre_skip_inter_sad * bh * bw * 3)) {
        r = c1enc_search_inter_b(b, pix, ctx, opt);
    }
    if (r < 0)
        return r;
    if (b->inter_cand_cnt > 0 //
        && b->inter_cand_stats[0].sad <= opt->thre_inter_efficient_sad * bh * bw * 3) {
        // debug("inter mode enough(%u), skip intra search", b->inter_cand_stats[0].sad);
        return r;
    }
    if (opt->try_intra //
        && (b->intra_cand_cnt == 0 || b->intra_cand_stats[0].sad > opt->thre_intra_efficient_sad * bh * bw * 3)) {
        r = c1enc_search_intra_b(b, pix, opt);
    }
    return r;
}
static inline int c1enc_search_p(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                                 const c1enc_search_option_t *opt) {
    int r = 0;
    if (p->is_partition) {
        for (int i = 0; i < 4; i++) {
            if ((r = c1enc_search_p(p->parts[i], pix, ctx, opt)) < 0) {
                return r;
            }
        }
    } else {
        if ((r = c1enc_search_b(p->b, pix, ctx, opt)) < 0)
            return r;
    }
    return r;
}

// --- search partition decisions ---

/** try merge to a partition node based on pred search result.
 merge occurs only when: (1) current partition is the last one, relating to 4 blocks. (2) for merge as intra mode, intra
 cands' sads are smooth and none is much worse than inter cands, and sum of SADs do not exceed certain fraction. (3) for
 merge as inter mode, vice versa. (4) search block partition mode at current level, result sad is not much worse than
 current partition. this process adds a pseudo block_t to current partition. case merge is determined, child partitions
 are cleaned ozrwis this block is cleaned.
 note: search merge is done recursively and depth-first(from bottom).
 @param p top partition instance to try merge
 @return negative for error. a failed merge is not an error.
*/
int c1enc_search_merge(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                       const c1enc_search_option_t *opt);
/** try divide a block(terminal partition) into 4 partitions. occurs when SAD exceeds certain fraction.
 note: search divide is done recursively and depth-first(from top), up to 8x8
 @param p top partition instance to try divide
 */
int c1enc_search_divide(c1enc_partition_t *p, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                        const c1enc_search_option_t *opt);

// --- top-level search ---

/** all-in-one search on super-block level.
 conceived process: (1) limited search, to get threshold of SAD per block. (2) try merge, then try divide, since merge
 takes more conditions which overlap divide condition(max SAD). (3) finer-grain search
*/
int c1enc_search_sb(c1enc_super_block_t *sb, const c1_pixbuf_t *pix, const c1enc_ctx_t *ctx, //
                    const c1enc_search_option_t *opt);

// --- profiler ---
extern c1_profile_t c1enc_search_sb_prof;
extern c1_profile_t c1enc_search_is_divide_prof;

    #ifdef __cplusplus
}
    #endif
#endif

/*
history:
2026.5.9: todo: require ref_frame awareness. now is incomplete.
*/