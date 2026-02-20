// libuv TCP advanced operations -- correct usage patterns (should COMPILE)
//
// Tests remaining uv_tcp_* functions not covered in libuv_tcp_pass.cpp:
// tcp_init_ex, tcp_open, tcp_nodelay, tcp_keepalive, tcp_simultaneous_accepts,
// tcp_getsockname, tcp_getpeername, tcp_connect, tcp_close_reset

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}
void dummy_connect_cb(uv_connect_t*, int) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: tcp_init_ex with address family
void variant_tcp_init_ex() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init_ex(&loop, &tcp, AF_INET);                         // @handle_state = 1

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 2: tcp_open on initialized handle
void variant_tcp_open() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1
    uv_tcp_open(&tcp, 0);                                          // requires @handle_state == 1

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: socket option functions (nodelay, keepalive, simultaneous_accepts)
void variant_tcp_socket_options() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_tcp_nodelay(&tcp, 1);                                       // requires @handle_state != 0
    uv_tcp_keepalive(&tcp, 1, 60);                                 // requires @handle_state != 0
    uv_tcp_simultaneous_accepts(&tcp, 1);                          // requires @handle_state != 0

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: tcp_getsockname and tcp_getpeername
void variant_tcp_name_queries() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    struct sockaddr name;
    int namelen = sizeof(name);

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_tcp_getsockname(&tcp, &name, &namelen);                    // requires @handle_state != 0
    uv_tcp_getpeername(&tcp, &name, &namelen);                    // requires @handle_state != 0

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: tcp_connect transitions handle to active state
void variant_tcp_connect() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    uv_connect_t connect_req;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_ip4_addr("127.0.0.1", 9999, &addr);
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_tcp_connect(&connect_req, &tcp, (const struct sockaddr*)&addr,
                    dummy_connect_cb);                              // @handle_state = 2

    uv_close((uv_handle_t*)&tcp, dummy_close_cb);                 // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 6: tcp_close_reset as alternative to uv_close
void variant_tcp_close_reset() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_tcp_close_reset(&tcp, dummy_close_cb);                     // @handle_state = 3

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_tcp_init_ex();
    variant_tcp_open();
    variant_tcp_socket_options();
    variant_tcp_name_queries();
    variant_tcp_connect();
    variant_tcp_close_reset();
    return 0;
}
