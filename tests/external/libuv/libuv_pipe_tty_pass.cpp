// libuv pipe and TTY operations -- correct usage patterns (should COMPILE)
//
// Tests all uv_pipe_* and uv_tty_* annotated functions with proper state transitions.

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}
void dummy_connect_cb(uv_connect_t*, int) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: pipe lifecycle: pipe_init -> pipe_bind -> listen -> close
void variant_pipe_bind_listen() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 0);                          // @handle_state = 1
    uv_pipe_bind(&pipe_handle, "/tmp/dtt_test.sock");              // requires @handle_state == 1
    uv_listen((uv_stream_t*)&pipe_handle, 128, dummy_connection_cb); // @handle_state = 2

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);         // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: pipe_open
void variant_pipe_open() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 0);                          // @handle_state = 1
    uv_pipe_open(&pipe_handle, 0);                                 // requires @handle_state == 1

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: pipe_bind2
void variant_pipe_bind2() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;
    const char* name = "/tmp/dtt_test2.sock";

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 0);                          // @handle_state = 1
    uv_pipe_bind2(&pipe_handle, name, 22, 0);                     // requires @handle_state == 1

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: pipe_connect
void variant_pipe_connect() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;
    uv_connect_t connect_req;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 0);                          // @handle_state = 1
    uv_pipe_connect(&connect_req, &pipe_handle, "/tmp/test.sock",
                    dummy_connect_cb);                              // @handle_state = 2

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: pipe_connect2
void variant_pipe_connect2() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;
    uv_connect_t connect_req;
    const char* name = "/tmp/test2.sock";

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 0);                          // @handle_state = 1
    uv_pipe_connect2(&connect_req, &pipe_handle, name, 19, 0,
                     dummy_connect_cb);                             // @handle_state = 2

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 6: pipe query functions: getsockname, getpeername, pending_count, pending_type, chmod
void variant_pipe_queries() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;
    char buf[256];
    size_t len = sizeof(buf);

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 1);                          // @handle_state = 1

    uv_pipe_getsockname(&pipe_handle, buf, &len);                  // requires @handle_state != 0
    uv_pipe_getpeername(&pipe_handle, buf, &len);                  // requires @handle_state != 0
    uv_pipe_pending_count(&pipe_handle);                           // requires @handle_state != 0
    uv_pipe_pending_type(&pipe_handle);                            // requires @handle_state != 0
    uv_pipe_chmod(&pipe_handle, UV_READABLE);                      // requires @handle_state != 0

    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 7: tty_init and mode operations
void variant_tty_init_and_mode() {
    uv_loop_t loop;
    uv_tty_t tty;
    int width, height;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tty_init(&loop, &tty, 0, 0);                               // @handle_state = 1

    uv_tty_set_mode(&tty, UV_TTY_MODE_NORMAL);                    // requires @handle_state != 0
    uv_tty_get_winsize(&tty, &width, &height);                    // requires @handle_state != 0

    uv_close((uv_handle_t*)&tty, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 8: tty global functions (no handle needed)
void variant_tty_global_functions() {
    uv_tty_reset_mode();                                           // no state requirements
    uv_tty_vtermstate_t state;
    uv_tty_get_vterm_state(&state);                                // requires _nonnull
    uv_tty_set_vterm_state(UV_TTY_SUPPORTED);                     // no state requirements
}

int main() {
    variant_pipe_bind_listen();
    variant_pipe_open();
    variant_pipe_bind2();
    variant_pipe_connect();
    variant_pipe_connect2();
    variant_pipe_queries();
    variant_tty_init_and_mode();
    variant_tty_global_functions();
    return 0;
}
