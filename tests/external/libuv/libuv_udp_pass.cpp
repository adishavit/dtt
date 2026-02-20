// libuv UDP operations -- correct usage patterns (should COMPILE)
//
// Tests all uv_udp_* annotated functions with proper state transitions.
// UDP handle lifecycle: init -> bind -> recv_start -> recv_stop -> close

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_alloc_cb(uv_handle_t*, size_t, uv_buf_t*) {}
void dummy_udp_recv_cb(uv_udp_t*, ssize_t, const uv_buf_t*, const struct sockaddr*, unsigned) {}
void dummy_udp_send_cb(uv_udp_send_t*, int) {}

// Variant 1: basic UDP lifecycle: init -> bind -> recv_start -> recv_stop -> close
void variant_udp_lifecycle() {
    uv_loop_t loop;
    uv_udp_t udp;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_ip4_addr("0.0.0.0", 5000, &addr);

    uv_udp_init(&loop, &udp);                                     // @handle_state = 1
    uv_udp_bind(&udp, (const struct sockaddr*)&addr, 0);          // requires @handle_state == 1
    uv_udp_recv_start(&udp, dummy_alloc_cb, dummy_udp_recv_cb);   // @handle_state = 2
    uv_udp_recv_stop(&udp);                                        // @handle_state = 1

    uv_close((uv_handle_t*)&udp, dummy_close_cb);                 // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: uv_udp_init_ex
void variant_udp_init_ex() {
    uv_loop_t loop;
    uv_udp_t udp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_udp_init_ex(&loop, &udp, AF_INET);                         // @handle_state = 1

    uv_close((uv_handle_t*)&udp, dummy_close_cb);                 // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: udp_open and socket options
void variant_udp_options() {
    uv_loop_t loop;
    uv_udp_t udp;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_udp_init(&loop, &udp);                                     // @handle_state = 1

    uv_udp_open(&udp, 0);                                         // requires @handle_state == 1

    uv_udp_set_broadcast(&udp, 1);                                 // requires @handle_state != 0
    uv_udp_set_ttl(&udp, 64);                                      // requires @handle_state != 0
    uv_udp_set_multicast_loop(&udp, 1);                            // requires @handle_state != 0
    uv_udp_set_multicast_ttl(&udp, 32);                            // requires @handle_state != 0
    uv_udp_set_multicast_interface(&udp, "0.0.0.0");               // requires @handle_state != 0

    uv_close((uv_handle_t*)&udp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: uv_udp_connect, getsockname, getpeername
void variant_udp_connect_and_names() {
    uv_loop_t loop;
    uv_udp_t udp;
    struct sockaddr_in addr;
    struct sockaddr name;
    int namelen = sizeof(name);

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_ip4_addr("127.0.0.1", 5001, &addr);

    uv_udp_init(&loop, &udp);                                     // @handle_state = 1
    uv_udp_connect(&udp, (const struct sockaddr*)&addr);           // requires @handle_state != 0
    uv_udp_getsockname(&udp, &name, &namelen);                    // requires @handle_state != 0
    uv_udp_getpeername(&udp, &name, &namelen);                    // requires @handle_state != 0

    uv_close((uv_handle_t*)&udp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: uv_udp_send and uv_udp_try_send
void variant_udp_send() {
    uv_loop_t loop;
    uv_udp_t udp;
    uv_udp_send_t send_req;
    struct sockaddr_in addr;
    char buf[64];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_ip4_addr("127.0.0.1", 5002, &addr);

    uv_udp_init(&loop, &udp);                                     // @handle_state = 1

    uv_udp_send(&send_req, &udp, &iov, 1,
                 (const struct sockaddr*)&addr, dummy_udp_send_cb); // requires handle_state != 0

    uv_udp_try_send(&udp, &iov, 1,
                     (const struct sockaddr*)&addr);                // requires handle_state != 0

    uv_close((uv_handle_t*)&udp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 6: uv_udp_set_membership and uv_udp_using_recvmmsg
void variant_udp_multicast_and_query() {
    uv_loop_t loop;
    uv_udp_t udp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_udp_init(&loop, &udp);                                     // @handle_state = 1

    uv_udp_set_membership(&udp, "239.0.0.1", "0.0.0.0", UV_JOIN_GROUP); // requires handle_state != 0
    uv_udp_using_recvmmsg(&udp);                                   // requires _nonnull

    uv_close((uv_handle_t*)&udp, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_udp_lifecycle();
    variant_udp_init_ex();
    variant_udp_options();
    variant_udp_connect_and_names();
    variant_udp_send();
    variant_udp_multicast_and_query();
    return 0;
}
