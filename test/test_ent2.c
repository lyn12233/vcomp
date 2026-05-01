#include "src/math/entcoder.h"
#include "src/util/mem.h"
#include "src/util/log.h"

#include <stdlib.h>

int main(){
    
    const int Nitem = 1000;
    uint16_t cdf[4] = {1, 12678, 1 << 15, 0};
    uint16_t icdf[4] = {(1 << 15) - 1, (1 << 15) - 12678, 0, 0};

    uint8_t *data = (uint8_t *)malloc(Nitem);
    srand(1145);
    for (int i = 0; i < Nitem; i++) {
        data[i] = rand() % 3;
    }

    c1ent_enc_t enc;
    c1ent_enc_init(&enc, 0);
    FILE *fp = fopen("logent1.txt", "w");
    FILE *fp2 = fopen("logentres1.txt", "w");
    for (int i = 0; i < Nitem; i++) {
        c1ent_encode_cdf(&enc, data[i], cdf, 3);
        c1ent_enc_repr(&enc, fp);
    }
    uint32_t sz;
    uint8_t *out = c1ent_enc_done(&enc, &sz);
    info_("encode result:");
    c1_dump_buf(out, sz);
}