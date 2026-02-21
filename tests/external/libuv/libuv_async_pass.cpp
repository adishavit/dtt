// libuv async operations -- correct usage patterns (should COMPILE)
//
// Tests uv_async_init and uv_async_send with proper state transitions.
// uv_async_init sets @handle_state = 2 (active immediately).

#include <uv.h>

void dummy_async_cb(uv_async_t*) {}
void dummy_close_cb(uv_handle_t*) {}

// Variant 1: async_init -> async_send -> close
void variant_async_lifecycle() {
    uv_loop_t loop;
    uv_async_t async;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_async_init(&loop, &async, dummy_async_cb);                  // @handle_state = 2

    uv_async_send(&async);                                         // requires @handle_state == 2

    uv_close((uv_handle_t*)&async, dummy_close_cb);               // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: multiple sends before close
void variant_async_multiple_sends() {
    uv_loop_t loop;
    uv_async_t async;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_async_init(&loop, &async, dummy_async_cb);                  // @handle_state = 2

    uv_async_send(&async);                                         // @handle_state == 2
    uv_async_send(&async);                                         // @handle_state == 2
    uv_async_send(&async);                                         // @handle_state == 2

    uv_close((uv_handle_t*)&async, dummy_close_cb);               // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_async_lifecycle();
    variant_async_multiple_sends();
    return 0;
}
