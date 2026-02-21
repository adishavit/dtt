// libuv TCP bind on wrong state — incorrect usage (should FAIL to compile)
//
// Tests that DTT catches tcp_bind after listen has made the handle active.
// After uv_listen, @handle_state == 2, but uv_tcp_bind requires @handle_state == 1.

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}

// Variant 1: tcp_bind after listen — handle already active
void variant_bind_after_listen() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                        // @loop_state = 1
    uv_ip4_addr("0.0.0.0", 8080, &addr);
    uv_tcp_init(&loop, &tcp);                                   // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, dummy_connection_cb);   // @handle_state = 2

    // DTT ERROR: requires arg0.@handle_state == 1, but @handle_state == 2
    uv_tcp_bind(&tcp, (const struct sockaddr*)&addr, 0);
}

int main() {
    variant_bind_after_listen();
    return 0;
}
