// Known gap isolated from unrelated setup diagnostics.
// libuv TCP bind on wrong state — incorrect usage (known missed diagnostic)
//
// Records the missing detection of tcp_bind after listen has made the handle active.
// After uv_listen, @handle_state == 2, but uv_tcp_bind requires @handle_state == 1.

#include <uv.h>

void dummy_connection_cb(uv_stream_t*, int) {}

// Variant 1: tcp_bind after listen — handle already active
void variant_bind_after_listen() {
    auto callback = &dummy_connection_cb;
    if (!callback) return;
    uv_loop_t loop;
    uv_tcp_t tcp;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                        // @loop_state = 1
    uv_ip4_addr("0.0.0.0", 8080, &addr);
    uv_tcp_init(&loop, &tcp);                                   // @handle_state = 1
    uv_listen((uv_stream_t*)&tcp, 128, callback);   // @handle_state = 2

    // KNOWN GAP (not asserted): expected error: requires arg0.@handle_state == 1, but @handle_state == 2
    uv_tcp_bind(&tcp, (const struct sockaddr*)&addr, 0);
}

int main() {
    variant_bind_after_listen();
    return 0;
}
