// FFmpeg SwResample operations before init (should FAIL to compile)
//
// Tests that DTT catches swr_convert called on a SwrContext that has been
// allocated (@swr_alive == 1) but not yet initialized (requires 2).
// Calling swr_convert without swr_init produces garbage output at best
// and crashes at worst.

extern "C" {
#include <libswresample/swresample.h>
}

// Variant 1: swr_convert before swr_init (state 1, requires 2)
void variant_convert_before_init() {
    struct SwrContext *swr = swr_alloc();  // @swr_alive = 1
    // Skipping swr_init -- context not initialized

    uint8_t *out_buf = nullptr;
    const uint8_t *in_buf = nullptr;

    // DTT ERROR: requires arg0.@swr_alive == 2, but @swr_alive == 1
    swr_convert(swr, &out_buf, 1024, &in_buf, 1024);

    swr_free(&swr);                        // @swr_alive = 0
}

int main() {
    variant_convert_before_init();
    return 0;
}
