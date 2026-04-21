#include "encode/types.h"
#include "src/encode/encoder.h"
#include "util/log.h"
#include "util/pixbuf.h"

#include <stdio.h>

int main() {
    c1_pixbuf_t frame = c1_pixbuf_create(C1_PIXBUF_C3I16, 8, 65);
    c1enc_frame_t frm = {0};
    c1enc_frame_update(&frm, &frame);
    c1enc_frame_repr(stdout, &frm, 0);
    assert_fatal(c1enc_frame_validate(&frm) >= 0);
    c1enc_frame_clear(&frm);
}