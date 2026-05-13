#include "src/encode/quant.h"
#include "src/encode/tables.h"


#include <stdint.h>

static void c1__invert_quant(uint32_t q) {
    // (1) get l=floorlog2
    uint32_t tmp = q, l = 0;
    while (tmp > 1)
        tmp >>= 1, l++;
    // (2) calc 16 bits mult which is 1.*2**(16) -> 0.*2**16, and avd 0
    uint32_t m = (1 << (16 + l + 1)) / q + 1;  // 16+l: fraction bits + shift
    uint16_t mult = (uint16_t)(m - (1 << 16)); // multiplier to remnant
    uint16_t shift = (uint16_t)l;              // left shift l
    debug("mult: %u, shift: %u", mult, shift);
}

static void c1__quantize_pix(int32_t c, c1_quant_t q) {
    const int sign = c < 0 ? -1 : 0;
    const int abs_c = c < 0 ? -c : c; // also (c^sign)-sign
    // (1) rounding and early exit
    if (abs_c < q.qstep / 2) {
        // *qc = *dqc = 0;
        return;
    }
    int64_t tmp64 = c1_clamp32(abs_c + q.qstep / 2, 0, INT16_MAX);
    // (2) divide by qstep
    tmp64 = (tmp64 * q.mult >> 16) + tmp64;
    int32_t tmp32 = (int32_t)(tmp64 >> q.shift);
    int32_t qc = (tmp32 ^ sign) - sign; // reserve the sign, fast alg
    // (3) dequantize
    tmp32 = tmp32 * q.qstep;
    int32_t dqc = (tmp32 ^ sign) - sign;
    info_("qc: %d dqc: %d", qc, dqc);
}

int main() {
    c1__invert_quant(66);
    c1__invert_quant(2);
    c1_quant_t q = {7, 61565, 66};
    info_("%u %u", q.qstep, q.mult);
    c1__quantize_pix(1 << 15, q);
}