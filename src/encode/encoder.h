/** @file encoder.h
 conceived encoder process:
  - for a new frame, init with 16x16 block size, which is the desired size.
  - to encode a frame:
    - decide frame type: i frame if p frame exceeds certain count or scene change detected
    - decide reference frame case a p frame.
    - foreach super block:
        - try palette mode at super block level. case sufficient, skip others (unimpl).
        - search limited intra/inter prediction modes.
        - try merge, then try divide to come with partition decision.
        - gather residuals(diff between origin and prediction)
        - search limited transform types (maybe only dct-dct).
        - decide quantization step from coefficients considering rate and distortion constraint.
        - negotiate base qi, qi, qi deltas at frame level.
        - search transform types given qi.
        - calculate quantized coefficients and dequantized coefficients
        - write to bitstream
        - inverse transform
        - push refernce frame to context
*/
#ifndef C1_ENCODE_ENCODER_H
#define C1_ENCODE_ENCODER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "types.h"

#include "math/transform.h"
#include "util/pixbuf.h"

#include <stdio.h>

extern c1_mpool_t c1enc_part_pool;

// --- --- instance constructors and destructors --- ---

// --- frame ---

// to init a frame, assign {0} then update it.
/** update or init a frame struct
 behaviors:
  - create a new expanded pixbuf align to super block boundary, filled with ?
  - re-alloc related super blocks and update them
 @param frm frame to update
 @param pix pure pixbuf to encode, in format c3i16
*/
int c1enc_frame_update(c1enc_frame_t *frm, const c1_pixbuf_t *pix);
int c1enc_frame_clear(c1enc_frame_t *frm);
int c1enc_frame_validate(const c1enc_frame_t *frame);
void c1enc_frame_repr(FILE *f, const c1enc_frame_t *frm, int ind);

// --- super block ---

/** update or init a super block
 a superblock is coding unit in a frame of size 64x64.
 it may include quantization index and globally allocated bufs
 behavior:
  - update pixbuf of sb
  - take into frame type info (i,p-frame)
  - unset q index delta?
  - case no root partition info, construct it
 @param sb super block to update
 @param frm frame info for sb
 @param y row offset in frame
*/
int c1enc_sb_update(c1enc_super_block_t *sb, const c1enc_frame_t *frm, uint16_t y, uint16_t x);
int c1enc_sb_clear(c1enc_super_block_t *sb);
int c1enc_sb_validate(const c1enc_super_block_t *sb);
void c1enc_sb_repr(FILE *f, const c1enc_super_block_t *sb, int ind);

// --- partition ---

/** init a partition that is {0}
 @param part partition instance
 @param sb super block back ref for buf in sb
 @param size current partition size
 @param targ_size the terminal partition size, at this size is not further partitioned
 @param y,x offset in super block, per pixel
 @param sb_y,sb_x super block offset in frame
 @param buf_offs occupation of bu f owned by sb
 */
int c1enc_partition_init(c1enc_partition_t *part, c1enc_super_block_t *sb, C1_2D_SZ size, C1_2D_SZ targ_size, //
                         uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,                                  //
                         uint16_t buf_offs);
/** reset candidate count in blocks recursively in partition
 */
int c1enc_partition_reset_cands(c1enc_partition_t *part);
int c1enc_partition_clear(c1enc_partition_t *part);
int c1enc_partition_validate(const c1enc_partition_t *part);
void c1enc_partition_repr(FILE *f, const c1enc_partition_t *part, int ind);

// --- block ---

int c1enc_block_update(c1enc_block_t *b, c1enc_super_block_t *sb, C1_2D_SZ size, //
                       uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,       //
                       uint16_t buf_offs);
int c1enc_block_clear(c1enc_block_t *b);
int c1enc_block_validate(c1enc_block_t *b);
void c1enc_block_repr(FILE *f, const c1enc_block_t *b, int ind);

// --- super block info/context accessing and referencing ---

/** clear an existing entry in context. should be internal func.
 */
int c1enc_ctx_clear_entry(c1enc_ctx_t *ctx, uint8_t idx);
/** push reference info of the frame frm into context ctx.
 for ref_t see @ref c1enc_ctx_s
 this process simply occupies an index at the back of arrays whose sizes are restriced by REF_FRAME_CNT,
 and silently destruct overflowed frame refs. other ops are not included, e.g. clear refs case i-frame.
*/
int c1enc_push_ref(c1enc_ctx_t *ctx, const c1enc_frame_t *frm);
/** init ref_t with the given partition_t, recursively
 */
int c1enc_ref_from_part(c1enc_ref_t *ref, const c1enc_partition_t *p);
/** destructor of ref_t
 */
int c1enc_ref_clear(c1enc_ref_t *ref);
/** get a ref_t at the position in ctx specified by ref_id and frame location specified by sb_y,
 sb_x, y and x.
 @param ref_id 1..16 the ref frame index from the current frame.
*/
c1enc_ref_t *c1enc_ref_at(c1enc_ctx_t *ctx, uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x, uint8_t ref_id);
/** get a block_t in frame at the location specified by sb_y, sb_x, y and x.
 */
c1enc_block_t *c1enc_block_at(c1enc_frame_t *frm, uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x);
/** get reference motion vector at given frame and location.
 this may be used in gathering mv candidates for inter prediction encoding.
 @param ref_id 0 for the current frame, >0 for a temporal ref frame
*/
c1enc_mv_t c1enc_get_mvref(const c1enc_frame_t *frm, const c1enc_ctx_t *ctx, //
                           uint16_t sb_y, uint16_t sb_x, uint8_t y, uint8_t x, uint8_t ref_id);

#ifdef __cplusplus
}
#endif

#endif