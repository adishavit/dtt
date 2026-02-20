// libuv stream operations -- correct usage patterns (should COMPILE)
//
// Tests stream functions shared by TCP, Pipe, TTY handles:
// uv_read_start, uv_read_stop, uv_write, uv_write2, uv_try_write,
// uv_try_write2, uv_shutdown, uv_listen, uv_accept,
// uv_is_readable, uv_is_writable, uv_stream_set_blocking,
// uv_stream_get_write_queue_size

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}
void dummy_close_cb(uv_handle_t*) {}
void dummy_alloc_cb(uv_handle_t*, size_t, uv_buf_t*) {}
void dummy_read_cb(uv_stream_t*, ssize_t, const uv_buf_t*) {}
void dummy_write_cb(uv_write_t*, int) {}
void dummy_shutdown_cb(uv_shutdown_t*, int) {}

// Variant 1: TCP server accept flow: listen -> accept with read_start/read_stop
void variant_tcp_accept_flow() {
    uv_loop_t loop;
    uv_tcp_t server;
    uv_tcp_t client;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_ip4_addr("0.0.0.0", 9000, &addr);

    uv_tcp_init(&loop, &server);                                  // server.@handle_state = 1
    uv_tcp_init(&loop, &client);                                  // client.@handle_state = 1
    uv_listen((uv_stream_t*)&server, 128, dummy_connection_cb);   // server.@handle_state = 2
    uv_accept((uv_stream_t*)&server, (uv_stream_t*)&client);     // requires server==2, client==1

    uv_read_start((uv_stream_t*)&client, dummy_alloc_cb, dummy_read_cb); // client.@handle_state = 2
    uv_read_stop((uv_stream_t*)&client);

    uv_close((uv_handle_t*)&client, dummy_close_cb);             // client.@handle_state = 3
    uv_close((uv_handle_t*)&server, dummy_close_cb);             // server.@handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                        // @loop_state = 3
}

// Variant 2: uv_write on an active stream
void variant_write_on_stream() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    uv_write_t write_req;
    char buf[64];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                     // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb);      // @handle_state = 2

    uv_write(&write_req, (uv_stream_t*)&tcp, &iov, 1, dummy_write_cb);  // requires handle_state != 0

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);                // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: uv_write2 with send_handle
void variant_write2() {
    uv_loop_t loop;
    uv_pipe_t pipe_handle;
    uv_pipe_t send_pipe;
    uv_write_t write_req;
    char buf[64];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_pipe_init(&loop, &pipe_handle, 1);                         // @handle_state = 1
    uv_pipe_init(&loop, &send_pipe, 1);                           // @handle_state = 1
    uv_listen((uv_stream_t*)&pipe_handle, 128, dummy_connection_cb); // @handle_state = 2

    uv_write2(&write_req, (uv_stream_t*)&pipe_handle, &iov, 1,
              (uv_stream_t*)&send_pipe, dummy_write_cb);          // requires handle_state != 0

    uv_close((uv_handle_t*)&send_pipe, dummy_close_cb);
    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: try_write and try_write2
void variant_try_write() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    uv_pipe_t pipe_handle;
    uv_pipe_t send_pipe;
    char buf[64];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                     // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb);      // @handle_state = 2

    uv_try_write((uv_stream_t*)&tcp, &iov, 1);                    // requires handle_state != 0

    uv_pipe_init(&loop, &pipe_handle, 1);                         // @handle_state = 1
    uv_pipe_init(&loop, &send_pipe, 1);                           // @handle_state = 1
    uv_listen((uv_stream_t*)&pipe_handle, 128, dummy_connection_cb); // @handle_state = 2

    uv_try_write2((uv_stream_t*)&pipe_handle, &iov, 1,
                   (uv_stream_t*)&send_pipe);                      // requires handle_state != 0

    uv_close((uv_handle_t*)&send_pipe, dummy_close_cb);
    uv_close((uv_handle_t*)&pipe_handle, dummy_close_cb);
    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: uv_shutdown on active stream
void variant_shutdown() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    uv_shutdown_t shutdown_req;

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                     // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb);      // @handle_state = 2

    uv_shutdown(&shutdown_req, (uv_stream_t*)&tcp, dummy_shutdown_cb); // requires handle_state != 0

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 6: stream query functions
void variant_stream_queries() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                          // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                     // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb);      // @handle_state = 2

    uv_is_readable((uv_stream_t*)&tcp);                            // requires _nonnull
    uv_is_writable((uv_stream_t*)&tcp);                            // requires _nonnull
    uv_stream_set_blocking((uv_stream_t*)&tcp, 0);                // requires handle_state != 0
    uv_stream_get_write_queue_size((uv_stream_t*)&tcp);            // requires _nonnull

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_tcp_accept_flow();
    variant_write_on_stream();
    variant_write2();
    variant_try_write();
    variant_shutdown();
    variant_stream_queries();
    return 0;
}
