// libuv poll, idle, prepare, check operations -- correct usage patterns (should COMPILE)
//
// Tests all four handle types that share init -> start -> stop -> close lifecycle:
// uv_poll_*, uv_idle_*, uv_prepare_*, uv_check_*

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_poll_cb(uv_poll_t*, int, int) {}
void dummy_idle_cb(uv_idle_t*) {}
void dummy_prepare_cb(uv_prepare_t*) {}
void dummy_check_cb(uv_check_t*) {}

// Variant 1: poll lifecycle: poll_init -> poll_start -> poll_stop -> close
void variant_poll_lifecycle() {
    uv_loop_t loop;
    uv_poll_t poll_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_poll_init(&loop, &poll_handle, 0);                          // @handle_state = 1
    uv_poll_start(&poll_handle, UV_READABLE, dummy_poll_cb);       // @handle_state = 2
    uv_poll_stop(&poll_handle);                                    // @handle_state = 1

    uv_close((uv_handle_t*)&poll_handle, dummy_close_cb);         // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: poll_init_socket variant
void variant_poll_init_socket() {
    uv_loop_t loop;
    uv_poll_t poll_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_poll_init_socket(&loop, &poll_handle, 0);                   // @handle_state = 1
    uv_poll_start(&poll_handle, UV_WRITABLE, dummy_poll_cb);       // @handle_state = 2
    uv_poll_stop(&poll_handle);                                    // @handle_state = 1

    uv_close((uv_handle_t*)&poll_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: idle lifecycle: idle_init -> idle_start -> idle_stop -> close
void variant_idle_lifecycle() {
    uv_loop_t loop;
    uv_idle_t idle_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_idle_init(&loop, &idle_handle);                             // @handle_state = 1
    uv_idle_start(&idle_handle, dummy_idle_cb);                    // @handle_state = 2
    uv_idle_stop(&idle_handle);                                    // @handle_state = 1

    uv_close((uv_handle_t*)&idle_handle, dummy_close_cb);         // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: prepare lifecycle: prepare_init -> prepare_start -> prepare_stop -> close
void variant_prepare_lifecycle() {
    uv_loop_t loop;
    uv_prepare_t prepare_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_prepare_init(&loop, &prepare_handle);                       // @handle_state = 1
    uv_prepare_start(&prepare_handle, dummy_prepare_cb);           // @handle_state = 2
    uv_prepare_stop(&prepare_handle);                              // @handle_state = 1

    uv_close((uv_handle_t*)&prepare_handle, dummy_close_cb);      // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: check lifecycle: check_init -> check_start -> check_stop -> close
void variant_check_lifecycle() {
    uv_loop_t loop;
    uv_check_t check_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_check_init(&loop, &check_handle);                          // @handle_state = 1
    uv_check_start(&check_handle, dummy_check_cb);                // @handle_state = 2
    uv_check_stop(&check_handle);                                 // @handle_state = 1

    uv_close((uv_handle_t*)&check_handle, dummy_close_cb);        // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 6: all four handle types in one loop
void variant_all_four_handles() {
    uv_loop_t loop;
    uv_poll_t poll_h;
    uv_idle_t idle_h;
    uv_prepare_t prepare_h;
    uv_check_t check_h;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_poll_init(&loop, &poll_h, 0);                               // @handle_state = 1
    uv_idle_init(&loop, &idle_h);                                  // @handle_state = 1
    uv_prepare_init(&loop, &prepare_h);                            // @handle_state = 1
    uv_check_init(&loop, &check_h);                                // @handle_state = 1

    uv_poll_start(&poll_h, UV_READABLE, dummy_poll_cb);            // @handle_state = 2
    uv_idle_start(&idle_h, dummy_idle_cb);                         // @handle_state = 2
    uv_prepare_start(&prepare_h, dummy_prepare_cb);                // @handle_state = 2
    uv_check_start(&check_h, dummy_check_cb);                     // @handle_state = 2

    uv_poll_stop(&poll_h);                                         // @handle_state = 1
    uv_idle_stop(&idle_h);                                         // @handle_state = 1
    uv_prepare_stop(&prepare_h);                                   // @handle_state = 1
    uv_check_stop(&check_h);                                      // @handle_state = 1

    uv_close((uv_handle_t*)&poll_h, dummy_close_cb);
    uv_close((uv_handle_t*)&idle_h, dummy_close_cb);
    uv_close((uv_handle_t*)&prepare_h, dummy_close_cb);
    uv_close((uv_handle_t*)&check_h, dummy_close_cb);

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

int main() {
    variant_poll_lifecycle();
    variant_poll_init_socket();
    variant_idle_lifecycle();
    variant_prepare_lifecycle();
    variant_check_lifecycle();
    variant_all_four_handles();
    return 0;
}
