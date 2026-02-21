// libuv full state machine walks -- correct usage patterns (should COMPILE)
//
// Tests multi-step state machine workflows that exercise ALL state values:
//   @loop_state:  0 -> 1 -> (operations) -> 1 -> 3
//   @handle_state: 0 -> 1 -> 2 -> 1 -> 3 (through close callback -> 4)
//
// Each variant exercises the complete lifecycle chain.

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_timer_cb(uv_timer_t*) {}
void dummy_connection_cb(uv_stream_t*, int) {}
void dummy_alloc_cb(uv_handle_t*, size_t, uv_buf_t*) {}
void dummy_read_cb(uv_stream_t*, ssize_t, const uv_buf_t*) {}
void dummy_idle_cb(uv_idle_t*) {}
void dummy_poll_cb(uv_poll_t*, int, int) {}
void dummy_signal_cb(uv_signal_t*, int) {}
void dummy_fs_event_cb(uv_fs_event_t*, const char*, int, int) {}

// Variant 1: full loop + timer state walk
// loop:  0 -> init -> 1 -> run -> 1 -> close -> 3
// timer: 0 -> init -> 1 -> start -> 2 -> stop -> 1 -> close -> 3
void variant_loop_timer_full_walk() {
    uv_loop_t loop;
    uv_timer_t timer;

    uv_loop_init(&loop);                                  // loop: 0 -> 1
    uv_timer_init(&loop, &timer);                         // timer: 0 -> 1
    uv_timer_start(&timer, dummy_timer_cb, 100, 0);       // timer: 1 -> 2
    uv_timer_stop(&timer);                                 // timer: 2 -> 1
    uv_close((uv_handle_t*)&timer, dummy_close_cb);       // timer: 1 -> 3
    uv_run(&loop, UV_RUN_NOWAIT);                          // loop: 1
    uv_loop_close(&loop);                                  // loop: 1 -> 3
}

// Variant 2: full loop + TCP server chain
// loop_init -> tcp_init -> tcp_bind -> listen -> close -> run -> loop_close
void variant_tcp_server_chain() {
    uv_loop_t loop;
    uv_tcp_t server;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                   // loop: 0 -> 1
    uv_ip4_addr("0.0.0.0", 8888, &addr);
    uv_tcp_init(&loop, &server);                           // server: 0 -> 1
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0); // server: still 1
    uv_listen((uv_stream_t*)&server, 128, dummy_connection_cb); // server: 1 -> 2
    uv_close((uv_handle_t*)&server, dummy_close_cb);      // server: 2 -> 3
    uv_run(&loop, UV_RUN_NOWAIT);                          // loop: 1
    uv_loop_close(&loop);                                  // loop: 1 -> 3
}

// Variant 3: full loop + fs operations chain
// loop_init -> fs_open -> fs_read -> fs_write -> fs_close -> fs_unlink -> loop_close
void variant_fs_chain() {
    uv_loop_t loop;
    uv_fs_t req;
    char buf[256];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                   // loop: 0 -> 1

    uv_fs_open(&loop, &req, "/tmp/chain.txt", 0, 0644, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_read(&loop, &req, 0, &iov, 1, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_write(&loop, &req, 0, &iov, 1, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_close(&loop, &req, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_unlink(&loop, &req, "/tmp/chain.txt", 0);
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                  // loop: 1 -> 3
}

// Variant 4: multiple handle types in one loop with full lifecycle
void variant_multi_handle_lifecycle() {
    uv_loop_t loop;
    uv_idle_t idle;
    uv_poll_t poll_h;
    uv_signal_t signal_h;
    uv_fs_event_t fs_ev;

    uv_loop_init(&loop);                                   // loop: 0 -> 1

    // Initialize all handles (all go to state 1)
    uv_idle_init(&loop, &idle);                            // idle: 0 -> 1
    uv_poll_init(&loop, &poll_h, 0);                       // poll: 0 -> 1
    uv_signal_init(&loop, &signal_h);                      // signal: 0 -> 1
    uv_fs_event_init(&loop, &fs_ev);                       // fs_ev: 0 -> 1

    // Start all handles (all go to state 2)
    uv_idle_start(&idle, dummy_idle_cb);                   // idle: 1 -> 2
    uv_poll_start(&poll_h, UV_READABLE, dummy_poll_cb);    // poll: 1 -> 2
    uv_signal_start(&signal_h, dummy_signal_cb, 2);        // signal: 1 -> 2
    uv_fs_event_start(&fs_ev, dummy_fs_event_cb, "/tmp", 0); // fs_ev: 1 -> 2

    // Stop all handles (all go back to state 1)
    uv_idle_stop(&idle);                                   // idle: 2 -> 1
    uv_poll_stop(&poll_h);                                 // poll: 2 -> 1
    uv_signal_stop(&signal_h);                             // signal: 2 -> 1
    uv_fs_event_stop(&fs_ev);                              // fs_ev: 2 -> 1

    // Close all handles (all go to state 3)
    uv_close((uv_handle_t*)&idle, dummy_close_cb);
    uv_close((uv_handle_t*)&poll_h, dummy_close_cb);
    uv_close((uv_handle_t*)&signal_h, dummy_close_cb);
    uv_close((uv_handle_t*)&fs_ev, dummy_close_cb);

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                  // loop: 1 -> 3
}

// Variant 5: default_loop provides initialized loop directly
void variant_default_loop_workflow() {
    uv_loop_t* loop = uv_default_loop();                   // loop: @loop_state = 1

    uv_loop_alive(loop);                                    // requires @loop_state == 1
    uv_now(loop);
    uv_update_time(loop);
    uv_run(loop, UV_RUN_NOWAIT);
}

int main() {
    variant_loop_timer_full_walk();
    variant_tcp_server_chain();
    variant_fs_chain();
    variant_multi_handle_lifecycle();
    variant_default_loop_workflow();
    return 0;
}
