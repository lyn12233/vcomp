/** @file
adaptive entropy encoder and decoder
*/
#ifndef C1_MATH_ENTCODER
#define C1_MATH_ENTCODER
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>

/** @defgroup entropy_coding
 entropy coding is to represent symbol with approximately -log2(p) bits,
 here uses arithmetic coding, mathematically computes a binary seq representing a
 number between (0,1). here assumes cdf of a alphabet (set of symbols) to encode/decode is
 determined at higher level, which is called context-adaptive.
 for implemenation details refer to [libaom]bitwriter.c,bitstream.c,entenc.c
 */

/** encoder context
 encoder encodes symbols and stores bits into a latent buffer in uin16_t.
*/
struct c1ent_enc_s {
    /** result buffer, avaialble after encode_done, which gathers carries from precarry_buf */
    unsigned char *buf;
    /** the encoded result is analogous to a number $p_0 256^{-1}+p_n 256^{-n-1}$
     use uint16_t to represent carries not forawrded at encode stage.
     */
    uint16_t *precarry_buf;
    uint32_t buf_sz;
    /** pre-carry buffer (reserved) size */
    uint32_t precarry_sz;
    /** offset in pre-carry buffer */
    uint32_t offs;
    /** part of the value of encode result.
     ordered like: | <unused+carry> | <effective bits> | (<headroom>) |
                   ^                ^                  ^              ^
                   32             cnt+16               0             cnt
     at normalization, shift out leading zeros in rng, shift d<=16 for both low and rng,
     and to prevent trunc bits above cnt+16 should be moved into precarry_buf when cnt+d>=0;
     */
    uint32_t low;
    /** symbol range. after completing a symbol, should be >=(1>>15) */
    uint16_t rng;
    /** count of headroom, inverse direction; after competing a symbol, should be (0,-16];
     when normalize, cnt is substracted at most twice at step 8 bits;
     when moving bits to precarry_buf, output bits of low above cnt+16;
     highest 1 in low is within cnt+32, indicating low of size 32, precarry buf of type uint16_t*;
     initial value is -9, ensures at least one byte is provided(?)
     */
    int16_t cnt;
};
typedef struct c1ent_enc_s c1ent_enc_t;

/** decoder context
 @todo though compliant to spec, some should be func loval vars.
 */
struct c1ent_dec_s {
    uint8_t num_bits;
    uint16_t buf;
    uint16_t padded_buf;
    uint16_t val; // symbol value
    uint16_t rng; // symbol range
    int max_bits;
};
typedef struct c1ent_dec_s c1ent_dec_t;

/** init encoder context.
 @param[out] enc uninitialized encoder instance
 @param sz estimated encoded bits size in bytes
 @return error code, always 0.
*/
int c1ent_enc_init(c1ent_enc_t *enc, uint32_t sz);

/** entropy-encode a symbol.
 @param[in,out] enc encoder instance, encode result stored within.
 @param[in] sym symbol index in the cdf description.
 @param[in] cdf cdf(cumulative density function) description, should have nbsym+1 elements, (..,1<<15,0).
 @param[in] nbsym number of symbols
 @return error code
*/
int c1ent_encode_cdf(c1ent_enc_t *enc, int sym, const uint16_t *cdf, int nbsym);

/** finish the currenty symbol encoding, propagate carries.
 @param[in,out] enc encoder instance, holds the result buffer
 @param[out] nbytes number of bytes of result. the result aligns to byte boundary
 @return pointer to the encoded buffer. aavail until encoder is cleared. NULL for error.
*/
uint8_t *c1ent_enc_done(c1ent_enc_t *enc, uint32_t *nbytes);

static void c1ent_enc_repr(const c1ent_enc_t *enc, FILE *f) {
    fprintf(f, "Encoder(bufferSize=%u,PrecarrySize=%u,offset=%u,low=%08x,rng=%04x,cnt=%d)\n", //
            enc->buf_sz, enc->precarry_sz, enc->offs, enc->low, enc->rng, enc->cnt);
}

/** release buf and precarry buf the encoder holds.
 @return error code
*/
int c1ent_enc_clear(c1ent_enc_t *enc);

/** init decoder context
 comfirmant to #8.2.2 (?)
 @param sz number of bytes to read
 @param read_bits reads at most 16 bits and store in return value, align to LSB, return -1 on error
 @param ctx context for read_bits function
 */
int c1ent_dec_init(c1ent_dec_t *dec, uint32_t sz, //
                   int (*read_bits)(void *ctx, uint32_t bits), void *ctx);

/** decoder exit process
 defined in av1 spec 8.2.3. seems to align strictly with byte boundary
*/
int c1ent_dec_exit(c1ent_dec_t *dec, //
                   int (*read_bits)(void *ctx, uint32_t bits), void *ctx);

/** decode a symbol
 confirmant to #8.2.6
 @param dec decoder context
 @param read_bits read n bits from bit stream, return err upon failure
 @param ctx bit stream reader context
 */
int c1ent_decode_cdf(c1ent_dec_t *dec,                                      //
                     int (*read_bits)(void *ctx, uint32_t bits), void *ctx, //
                     uint16_t *cdf, int nbsym);

// --- misc

/** update the cdf accroding to the current symbol
 */
void c1ent_update_cdf(uint16_t *cdf, uint8_t sym, uint8_t nbsym);

/** a simple wrapper for string as bitstream */
typedef struct {
    const uint8_t *data;
    // size of data and offset in data (per bits)
    uint32_t sz; // const
    uint32_t offs;
} c1ent_strstrm_t;

/** read no more than 16 bits, compatible to read_bits param in dec functions */
int c1ent_strstrm_read_bits16(void *ctx, uint32_t bits);

#ifdef __cplusplus
}
#endif
#endif