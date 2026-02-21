// libuv timer lifecycle — correct usage patterns (should COMPILE)
//
// Tests that the timer init -> start -> stop -> close lifecycle is tracked
// correctly by DTT ghost fields @loop_state and @handle_state.

#include <uv.h>

void dummy_timer_cb(uv_timer_t*) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: loop_init -> timer_init -> timer_start -> timer_stop -> close -> loop_close
void variant_full_timer_lifecycle() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                  // loop.@loop_state = 1
    uv_timer_init(&loop, &timer);                         // requires loop.@loop_state == 1, timer.@handle_state = 1
    uv_timer_start(&timer, dummy_timer_cb, 1000, 0);     // requires @handle_state == 1, ensures @handle_state = 2
    uv_timer_stop(&timer);                                // requires @handle_state == 2, ensures @handle_state = 1
    uv_close((uv_handle_t*)&timer, dummy_close_cb);      // requires @handle_state != 0/3/4, ensures @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);                         // requires @loop_state == 1
    uv_loop_close(&loop);                                 // requires @loop_state == 1, ensures @loop_state = 3
}

int main() {
    variant_full_timer_lifecycle();
    return 0;
}
