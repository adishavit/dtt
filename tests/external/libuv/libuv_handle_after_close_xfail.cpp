// Known gap isolated from unrelated setup diagnostics.
// libuv handle operations after uv_close -- incorrect usage (known missed diagnostic)
//
// Records the missing detection of handle operations performed after uv_close.
// After uv_close, @handle_state == 3, but handle operations require
// @handle_state == 1 or @handle_state == 2.

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_idle_cb(uv_idle_t*) {}

// Variant 1: idle_start after uv_close
void variant_idle_start_after_close() {
    auto callback = &dummy_idle_cb;
    if (!callback) return;
    uv_loop_t loop;
    uv_idle_t idle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_idle_init(&loop, &idle);                                    // @handle_state = 1
    uv_close((uv_handle_t*)&idle, dummy_close_cb);                // @handle_state = 3

    // KNOWN GAP (not asserted): expected error: requires arg0.@handle_state == 1, but @handle_state == 3
    uv_idle_start(&idle, callback);
}

int main() {
    variant_idle_start_after_close();
    return 0;
}
