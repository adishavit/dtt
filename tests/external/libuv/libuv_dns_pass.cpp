// libuv DNS operations -- correct usage patterns (should COMPILE)
//
// Tests uv_getaddrinfo, uv_getnameinfo, uv_freeaddrinfo with proper loop state.

#include <uv.h>

void dummy_getaddrinfo_cb(uv_getaddrinfo_t*, int, struct addrinfo*) {}
void dummy_getnameinfo_cb(uv_getnameinfo_t*, int, const char*, const char*) {}

// Variant 1: uv_getaddrinfo with callback
void variant_getaddrinfo() {
    uv_loop_t loop;
    uv_getaddrinfo_t resolver;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_getaddrinfo(&loop, &resolver, dummy_getaddrinfo_cb,
                    "example.com", "80", 0);                        // requires @loop_state == 1

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: uv_getaddrinfo synchronous (NULL callback) + freeaddrinfo
void variant_getaddrinfo_sync_with_free() {
    uv_loop_t loop;
    uv_getaddrinfo_t resolver;
    struct addrinfo hints;
    struct addrinfo result;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_getaddrinfo(&loop, &resolver, 0, "localhost", "443", &hints); // requires @loop_state == 1
    uv_freeaddrinfo(&result);                                      // requires _nonnull

    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 3: uv_getnameinfo
void variant_getnameinfo() {
    uv_loop_t loop;
    uv_getnameinfo_t nameinfo;
    struct sockaddr_in addr;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_ip4_addr("127.0.0.1", 80, &addr);

    uv_getnameinfo(&loop, &nameinfo, dummy_getnameinfo_cb,
                    (const struct sockaddr*)&addr, 0);              // requires @loop_state == 1

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

int main() {
    variant_getaddrinfo();
    variant_getaddrinfo_sync_with_free();
    variant_getnameinfo();
    return 0;
}
