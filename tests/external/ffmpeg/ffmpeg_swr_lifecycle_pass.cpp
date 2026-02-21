// FFmpeg SwResample lifecycle -- correct usage patterns (should COMPILE)
//
// Tests that DTT ghost field @swr_alive correctly tracks the SwResample
// context through alloc (1) -> init (2) -> convert -> close (1) -> free (0).

extern "C" {
#include <libswresample/swresample.h>
}

// Variant 1: swr_alloc -> swr_init -> swr_convert -> swr_close -> swr_free
void variant_full_lifecycle() {
    struct SwrContext *swr = swr_alloc();  // @swr_alive = 1
    swr_init(swr);                         // requires 1, ensures @swr_alive = 2

    uint8_t out_buffer[1024];
    uint8_t *out_buf = out_buffer;
    const uint8_t in_buffer[1024] = {};
    const uint8_t *in_buf = in_buffer;
    swr_convert(swr, &out_buf, 1024,       // requires @swr_alive == 2
                &in_buf, 1024);

    swr_close(swr);                        // requires 2, ensures @swr_alive = 1
    swr_free(&swr);                        // @swr_alive = 0
}

// Variant 2: swr_alloc -> swr_init -> swr_free (skip close, free handles it)
void variant_init_then_free() {
    struct SwrContext *swr = swr_alloc();  // @swr_alive = 1
    swr_init(swr);                         // @swr_alive = 2
    swr_free(&swr);                        // @swr_alive = 0
}

int main() {
    variant_full_lifecycle();
    variant_init_then_free();
    return 0;
}
