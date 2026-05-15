#include <stdint.h>
#include <stdio.h>

#include "encode/types.h"
#include "src/util/log.h"
#include "src/util/pixbuf.h"

int main() {
    c1_pixbuf_t pb = c1_pixbuf_create(C1_PIXBUF_C1I8, 8, 8);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            uint8_t *p = c1_pixbuf_get(&pb, y, x);
            *p = (uint8_t)((y << 4) + x);
        }
    }
    c1_pixbuf_repr(stdout, &pb);
    c1_pixbuf_t pb2 = c1_pixbuf_fromview(&pb, (int[3]){0, 8, 2}, (int[3]){0, 8, 2});
    c1_pixbuf_repr(stdout, &pb2);
    c1_pixbuf_t pb3 = c1_pixbuf_fromview(&pb, (int[3]){0, 8, 1}, (int[3]){7, 0xffff, -1});
    c1_pixbuf_repr(stdout, &pb3);
    c1_pixbuf_t pb4 = c1_pixbuf_fromview(&pb, (int[3]){-4, -1, 1}, (int[3]){0, 1, 1});
    c1_pixbuf_repr(stdout, &pb4);
    c1_pixbuf_t pb5 = c1_pixbuf_create(C1_PIXBUF_C1I8, 10, 10);
    c1_pixbuf_paste(&pb5, &pb, -4, -4);
    c1_pixbuf_paste(&pb5, &pb, 6, 8);
    info_("pb5");
    c1_pixbuf_repr(stdout, &pb5);
    c1_pixbuf_clear(&pb);
    c1_pixbuf_clear(&pb2);
    c1_pixbuf_clear(&pb3);
    c1_pixbuf_clear(&pb4);
    c1_pixbuf_clear(&pb5);

    for (int16_t r = 0; r < 256; r+=4) {
        for (int16_t g = 0; g < 256; g += 4) {
            for (int16_t b = 0; b < 256; b += 4) {
                pb = c1_pixbuf_create(C1_PIXBUF_C3I8, 1, 1);
                uint8_t *ptr = c1_pixbuf_get(&pb, 0, 0);
                ptr[0] = (uint8_t)r, ptr[1] = (uint8_t)g, ptr[2] = (uint8_t)b;
                // c1_pixbuf_repr(stdout, &pb);
                pb2 = c1_pixbuf_cvt_rgbi8_to_yuv16(&pb);
                // c1_pixbuf_repr(stdout, &pb2);
                c1_pixbuf_clear(&pb);
                pb = c1_pixbuf_cvt_yuv16_to_rgbi8(&pb2);
                // c1_pixbuf_repr(stdout, &pb);
                ptr = c1_pixbuf_get(&pb, 0, 0);
                if (c1_abs_dif_i16(ptr[0], r) + c1_abs_dif_i16(ptr[1], g) + c1_abs_dif_i16(ptr[2], b) > 4) {
                    warning("dev: %u,%u,%u -> %u,%u,%u", r, g, b, ptr[0], ptr[1], ptr[2]);
                }
                // assert_fatal();
                c1_pixbuf_clear(&pb);
                c1_pixbuf_clear(&pb2);
            }
        }
    }
}