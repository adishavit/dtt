// libuv loop advanced operations -- correct usage patterns (should COMPILE)
//
// Tests additional loop query/configuration functions beyond basic lifecycle.
// All require loop.@loop_state == 1 (initialized/stopped).

#include <uv.h>

void dummy_walk_cb(uv_handle_t*, void*) {}

// Variant 1: loop query functions (alive, backend_fd, backend_timeout, now, update_time)
void variant_loop_queries() {
    uv_loop_t loop;
    uv_loop_init(&loop);               // @loop_state = 1

    uv_loop_alive(&loop);              // requires @loop_state == 1
    uv_backend_fd(&loop);              // requires @loop_state == 1
    uv_backend_timeout(&loop);         // requires @loop_state == 1
    uv_now(&loop);                     // requires @loop_state == 1
    uv_update_time(&loop);             // requires @loop_state == 1

    uv_loop_close(&loop);             // @loop_state = 3
}

// Variant 2: uv_walk and uv_stop on initialized loop
void variant_walk_and_stop() {
    uv_loop_t loop;
    uv_loop_init(&loop);               // @loop_state = 1

    uv_walk(&loop, dummy_walk_cb, nullptr); // requires @loop_state == 1
    uv_stop(&loop);                    // requires @loop_state == 1

    uv_loop_close(&loop);             // @loop_state = 3
}

// Variant 3: uv_loop_configure
void variant_loop_configure() {
    uv_loop_t loop;
    uv_loop_init(&loop);               // @loop_state = 1

    uv_loop_configure(&loop, UV_LOOP_BLOCK_SIGNAL); // requires @loop_state == 1

    uv_loop_close(&loop);             // @loop_state = 3
}

// Variant 4: full loop state machine walk: 0 -> 1 -> (queries) -> 1 -> 3
void variant_full_loop_state_walk() {
    uv_loop_t loop;
    // loop starts uninitialized (@loop_state == 0)
    uv_loop_init(&loop);               // @loop_state = 1

    uv_loop_alive(&loop);              // @loop_state == 1 (query, no state change)
    uv_now(&loop);                     // @loop_state == 1
    uv_update_time(&loop);             // @loop_state == 1
    uv_backend_fd(&loop);              // @loop_state == 1
    uv_backend_timeout(&loop);         // @loop_state == 1
    uv_run(&loop, UV_RUN_NOWAIT);      // @loop_state == 1
    uv_walk(&loop, dummy_walk_cb, nullptr); // @loop_state == 1
    uv_stop(&loop);                    // @loop_state == 1

    uv_loop_close(&loop);             // @loop_state = 3
}

int main() {
    variant_loop_queries();
    variant_walk_and_stop();
    variant_loop_configure();
    variant_full_loop_state_walk();
    return 0;
}
