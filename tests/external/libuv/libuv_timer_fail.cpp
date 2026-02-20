// libuv timer operations on wrong state — incorrect usage (should FAIL to compile)
//
// Tests that DTT catches timer_start on a handle that has been closed.
// After uv_close, @handle_state == 3, but uv_timer_start requires @handle_state == 1.

#include <uv.h>

void dummy_timer_cb(uv_timer_t*) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: timer_start after uv_close — handle is in closing state
void variant_start_after_close() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                // @loop_state = 1
    uv_timer_init(&loop, &timer);                       // @handle_state = 1
    uv_close((uv_handle_t*)&timer, dummy_close_cb);    // @handle_state = 3

    // DTT ERROR: requires arg0.@handle_state == 1, but @handle_state == 3
    uv_timer_start(&timer, dummy_timer_cb, 1000, 0);
}

int main() {
    variant_start_after_close();
    return 0;
}
