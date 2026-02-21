// libuv signal operations -- correct usage patterns (should COMPILE)
//
// Tests uv_signal_init, uv_signal_start, uv_signal_start_oneshot,
// uv_signal_stop with proper state transitions.

#include <uv.h>

void dummy_signal_cb(uv_signal_t*, int) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: signal_init -> signal_start -> signal_stop -> close
void variant_signal_lifecycle() {
    uv_loop_t loop;
    uv_signal_t signal_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_signal_init(&loop, &signal_handle);                         // @handle_state = 1
    uv_signal_start(&signal_handle, dummy_signal_cb, 2);           // @handle_state = 2
    uv_signal_stop(&signal_handle);                                // @handle_state = 1

    uv_close((uv_handle_t*)&signal_handle, dummy_close_cb);       // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: signal_start_oneshot
void variant_signal_oneshot() {
    uv_loop_t loop;
    uv_signal_t signal_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_signal_init(&loop, &signal_handle);                         // @handle_state = 1
    uv_signal_start_oneshot(&signal_handle, dummy_signal_cb, 15);  // @handle_state = 2
    uv_signal_stop(&signal_handle);                                // @handle_state = 1

    uv_close((uv_handle_t*)&signal_handle, dummy_close_cb);       // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: full handle state walk: 0 -> 1 -> 2 -> 1 -> 3
void variant_signal_full_state_walk() {
    uv_loop_t loop;
    uv_signal_t sig;

    uv_loop_init(&loop);                                           // @loop_state = 1

    // @handle_state starts at 0 (uninitialized)
    uv_signal_init(&loop, &sig);                                   // @handle_state = 1
    uv_signal_start(&sig, dummy_signal_cb, 2);                     // @handle_state = 2
    uv_signal_stop(&sig);                                          // @handle_state = 1
    uv_close((uv_handle_t*)&sig, dummy_close_cb);                 // @handle_state = 3

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

int main() {
    variant_signal_lifecycle();
    variant_signal_oneshot();
    variant_signal_full_state_walk();
    return 0;
}
