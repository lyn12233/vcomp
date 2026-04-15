#ifndef C1_ENCODE_ENCODER_H
#define C1_ENCODE_ENCODER_H
#include "types.h"
#include "util/pixbuf.h"
#ifdef __cplusplus
extern "C" {
#endif

#include "src/encode/types.h"

#include <stdio.h>

// to init a frame, assign {0} then update it.
/** update a frame struct
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

int c1enc_sb_update(c1enc_super_block_t *sb, const c1enc_frame_t *frm, int x, int y);
int c1enc_sb_clear(c1enc_super_block_t *sb);
int c1enc_sb_validate(const c1enc_super_block_t *sb);
void c1enc_sb_repr(FILE *f, const c1enc_super_block_t *sb, int ind);

int c1enc_sb_pass0();
int c1enc_sb_pass1();
int c1enc_sb_pass2();

int c1enc_mb_update(c1enc_macro_block_t *mb, const c1enc_super_block_t *sb);
int c1enc_mb_validate(const c1enc_macro_block_t *mb);
void c1enc_mb_repr(FILE *f, const c1enc_macro_block_t *mb, int ind);

#ifdef __cplusplus
}
#endif

#endif