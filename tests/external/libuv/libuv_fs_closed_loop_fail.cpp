// libuv fs operations on closed loop -- incorrect usage (should FAIL to compile)
//
// Tests that DTT catches filesystem operations performed on a closed loop.
// After uv_loop_close, @loop_state == 3, but fs operations require @loop_state == 1.

#include <uv.h>

// Variant 1: uv_fs_open on a closed loop
void variant_fs_open_closed_loop() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);            // @loop_state = 1
    uv_loop_close(&loop);           // @loop_state = 3

    // DTT ERROR: requires arg0.@loop_state == 1, but @loop_state == 3
    uv_fs_open(&loop, &req, "/tmp/test.txt", 0, 0, 0);  // dtt-error {{requires @loop_state}}
}

int main() {
    variant_fs_open_closed_loop();
    return 0;
}
