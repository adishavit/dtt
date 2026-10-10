// libuv handle use after close — incorrect usage (should FAIL to compile)
//
// Tests that DTT catches timer_stop on a handle that has been closed.
// After uv_close, @handle_state == 3, but uv_timer_stop requires @handle_state == 2.

#include <uv.h>

void dummy_timer_cb(uv_timer_t*) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: timer_stop after uv_close — handle is in closing state
void variant_stop_after_close() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                   // @loop_state = 1
    uv_timer_init(&loop, &timer);                          // @handle_state = 1
    uv_timer_start(&timer, dummy_timer_cb, 1000, 0);      // @handle_state = 2
    uv_close((uv_handle_t*)&timer, dummy_close_cb);       // @handle_state = 3

    // DTT ERROR: requires arg0.@handle_state == 2 (required state is not established in enforce mode)
    uv_timer_stop(&timer);  // dtt-error {{@handle_state}}
}

int main() {
    variant_stop_after_close();
    return 0;
}
