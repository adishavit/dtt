// libuv timer advanced operations -- correct usage patterns (should COMPILE)
//
// Tests remaining timer functions not covered in libuv_timer_pass.cpp:
// uv_timer_again, uv_timer_set_repeat, uv_timer_get_repeat, uv_timer_get_due_in

#include <uv.h>

void dummy_timer_cb(uv_timer_t*) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: timer_set_repeat and timer_get_repeat on initialized handle
void variant_timer_repeat() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_timer_init(&loop, &timer);                                  // @handle_state = 1

    uv_timer_set_repeat(&timer, 500);                              // requires @handle_state != 0
    uv_timer_get_repeat(&timer);                                   // requires @handle_state != 0

    uv_close((uv_handle_t*)&timer, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 2: timer_again on active timer
void variant_timer_again() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_timer_init(&loop, &timer);                                  // @handle_state = 1
    uv_timer_start(&timer, dummy_timer_cb, 1000, 500);            // @handle_state = 2

    uv_timer_again(&timer);                                        // requires @handle_state != 0

    uv_timer_stop(&timer);                                         // @handle_state = 1
    uv_close((uv_handle_t*)&timer, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: timer_get_due_in on active timer
void variant_timer_get_due_in() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_timer_init(&loop, &timer);                                  // @handle_state = 1
    uv_timer_start(&timer, dummy_timer_cb, 1000, 0);              // @handle_state = 2

    uv_timer_get_due_in(&timer);                                   // requires @handle_state != 0

    uv_timer_stop(&timer);
    uv_close((uv_handle_t*)&timer, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: full timer workflow with all query functions
void variant_timer_full_workflow() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_timer_init(&loop, &timer);                                  // @handle_state = 1

    uv_timer_set_repeat(&timer, 200);                              // @handle_state != 0
    uv_timer_get_repeat(&timer);                                   // @handle_state != 0
    uv_timer_start(&timer, dummy_timer_cb, 100, 200);             // @handle_state = 2
    uv_timer_get_due_in(&timer);                                   // @handle_state != 0
    uv_timer_again(&timer);                                        // @handle_state != 0
    uv_timer_stop(&timer);                                         // @handle_state = 1

    uv_close((uv_handle_t*)&timer, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_timer_repeat();
    variant_timer_again();
    variant_timer_get_due_in();
    variant_timer_full_workflow();
    return 0;
}
