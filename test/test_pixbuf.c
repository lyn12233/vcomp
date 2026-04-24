#include <stdio.h>

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
    info("pb5");
    c1_pixbuf_repr(stdout, &pb5);
    c1_pixbuf_clear(&pb);
    c1_pixbuf_clear(&pb2);
    c1_pixbuf_clear(&pb3);
    c1_pixbuf_clear(&pb4);
    c1_pixbuf_clear(&pb5);
}