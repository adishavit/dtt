// libuv loop lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the uv_loop_init -> uv_run -> uv_loop_close lifecycle is tracked
// correctly by DTT ghost field @loop_state. Each variant properly
// initializes, uses, and closes the event loop.

#include <uv.h>

// Variant 1: uv_loop_init -> uv_run(UV_RUN_NOWAIT) -> uv_loop_close
void variant_basic_loop() {
    uv_loop_t loop;
    uv_loop_init(&loop);                // @loop_state = 1
    uv_run(&loop, UV_RUN_NOWAIT);       // requires @loop_state == 1
    uv_loop_close(&loop);               // requires @loop_state == 1, ensures @loop_state = 3
}

// Variant 2: uv_default_loop returns an initialized loop
void variant_default_loop() {
    uv_loop_t *loop = uv_default_loop();  // @loop_state = 1
    uv_run(loop, UV_RUN_NOWAIT);          // requires @loop_state == 1
}

int main() {
    variant_basic_loop();
    variant_default_loop();
    return 0;
}
