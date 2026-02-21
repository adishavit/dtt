// libuv filesystem operations — correct usage patterns (should COMPILE)
//
// Tests that filesystem operations require loop.@loop_state == 1.
// Uses synchronous mode (NULL callback).

#include <uv.h>

// Variant 1: loop_init -> uv_fs_stat -> uv_fs_req_cleanup -> loop_close
void variant_fs_stat() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                             // @loop_state = 1
    uv_fs_stat(&loop, &req, "/tmp", 0);              // requires @loop_state == 1
    uv_fs_req_cleanup(&req);
    uv_loop_close(&loop);                            // requires @loop_state == 1, ensures @loop_state = 3
}

int main() {
    variant_fs_stat();
    return 0;
}
