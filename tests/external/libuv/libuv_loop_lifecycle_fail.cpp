// libuv loop lifecycle — incorrect usage patterns (should FAIL to compile)
//
// Tests that DTT catches use of a closed event loop.
// After uv_loop_close, @loop_state == 3, but uv_run requires @loop_state == 1.

#include <uv.h>

// Variant 1: uv_run after uv_loop_close — loop already closed
void variant_run_after_close() {
    uv_loop_t loop;
    uv_loop_init(&loop);            // @loop_state = 1
    uv_loop_close(&loop);           // @loop_state = 3

    // DTT ERROR: requires arg0.@loop_state == 1, but @loop_state == 3
    uv_run(&loop, UV_RUN_NOWAIT);
}

int main() {
    variant_run_after_close();
    return 0;
}
