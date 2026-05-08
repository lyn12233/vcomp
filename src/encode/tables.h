/** @file contexts.h
 const ecnoder context mapping and lookups
*/
#ifndef C1_ENCODER_TABLES_H
#define C1_ENCODER_TABLES_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define C1_NUM_Q_DELTA 3

extern const uint16_t c1_lookup_q_dc[3][256];
extern const uint16_t c1_lookup_q_ac[3][256];
extern const uint16_t c1_lookup_scan_8_8[3][64];
extern const uint16_t c1_lookup_scan_16_16[3][256];
extern const uint16_t c1_lookup_scan_32_32[3][1024];
// scan 64x64 is the same as 32x32 where higher orders are zeroed.
// extern const uint16_t c1_lookup_scan_64_64[3][4096];

// all in one scan tables
extern const uint16_t (*c1_lookup_scans[4][3]);

//
#ifdef __cplusplus
}
#endif
#endif