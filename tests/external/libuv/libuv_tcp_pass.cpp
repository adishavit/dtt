// libuv TCP server setup — correct usage patterns (should COMPILE)
//
// Tests that the TCP init -> bind -> listen -> close lifecycle is tracked
// correctly by DTT ghost fields @loop_state and @handle_state.

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: loop_init -> tcp_init -> tcp_bind -> listen -> close -> loop_close
void variant_tcp_server() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                   // loop.@loop_state = 1
    uv_ip4_addr("0.0.0.0", 8080, &addr);
    uv_tcp_init(&loop, &tcp);                              // requires loop.@loop_state == 1, tcp.@handle_state = 1
    uv_tcp_bind(&tcp, (const struct sockaddr*)&addr, 0);   // requires @handle_state == 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb); // requires @handle_state != 0, ensures @handle_state = 2
    uv_close((uv_handle_t*)&tcp, dummy_close_cb);         // requires @handle_state != 0/3/4, ensures @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);                          // requires @loop_state == 1
    uv_loop_close(&loop);                                  // requires @loop_state == 1, ensures @loop_state = 3
}

int main() {
    variant_tcp_server();
    return 0;
}
