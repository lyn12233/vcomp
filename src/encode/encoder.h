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
int c1enc_sb_update(c1enc_super_block_t *sb, const c1enc_frame_t *frm, int y, int x);
int c1enc_sb_clear(c1enc_super_block_t *sb);
int c1enc_sb_validate(const c1enc_super_block_t *sb);
void c1enc_sb_repr(FILE *f, const c1enc_super_block_t *sb, int ind);

int c1enc_sb_pass0();
int c1enc_sb_pass1();
int c1enc_sb_pass2();

int c1enc_partition_update(c1enc_partition_t *part, c1enc_super_block_t *sb, C1_2D_SZ size, //
                           uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,                    //
                           uint16_t buf_offs);
int c1enc_partition_clear(c1enc_partition_t *part);
int c1enc_partition_validate(const c1enc_partition_t *part);
void c1enc_partition_repr(FILE *f, const c1enc_partition_t *part, int ind);

int c1enc_block_update(c1enc_block_t *b, c1enc_super_block_t *sb, C1_2D_SZ size, //
                       uint8_t y, uint8_t x, uint16_t sb_y, uint16_t sb_x,             //
                       uint16_t buf_offs);
int c1enc_block_clear(c1enc_block_t *b);
int c1enc_block_validate(c1enc_block_t *b);
void c1enc_block_repr(FILE *f, const c1enc_block_t *b, int ind);

#ifdef __cplusplus
}
#endif

#endif