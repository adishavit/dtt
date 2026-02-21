// libuv fs_event and fs_poll operations -- correct usage patterns (should COMPILE)
//
// Tests uv_fs_event_* and uv_fs_poll_* annotated functions with proper state transitions.

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_fs_event_cb(uv_fs_event_t*, const char*, int, int) {}
void dummy_fs_poll_cb(uv_fs_poll_t*, int, const uv_stat_t*, const uv_stat_t*) {}

// Variant 1: fs_event lifecycle: init -> start -> stop -> close
void variant_fs_event_lifecycle() {
    uv_loop_t loop;
    uv_fs_event_t fs_event;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_fs_event_init(&loop, &fs_event);                            // @handle_state = 1
    uv_fs_event_start(&fs_event, dummy_fs_event_cb, "/tmp", 0);   // @handle_state = 2
    uv_fs_event_stop(&fs_event);                                   // @handle_state = 1

    uv_close((uv_handle_t*)&fs_event, dummy_close_cb);            // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: fs_event_getpath on active handle
void variant_fs_event_getpath() {
    uv_loop_t loop;
    uv_fs_event_t fs_event;
    char buf[256];
    size_t len = sizeof(buf);

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_fs_event_init(&loop, &fs_event);                            // @handle_state = 1
    uv_fs_event_start(&fs_event, dummy_fs_event_cb, "/tmp", 0);   // @handle_state = 2

    uv_fs_event_getpath(&fs_event, buf, &len);                    // requires @handle_state != 0

    uv_fs_event_stop(&fs_event);                                   // @handle_state = 1
    uv_close((uv_handle_t*)&fs_event, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: fs_poll lifecycle: init -> start -> stop -> close
void variant_fs_poll_lifecycle() {
    uv_loop_t loop;
    uv_fs_poll_t fs_poll;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_fs_poll_init(&loop, &fs_poll);                              // @handle_state = 1
    uv_fs_poll_start(&fs_poll, dummy_fs_poll_cb, "/tmp/test", 1000); // @handle_state = 2
    uv_fs_poll_stop(&fs_poll);                                     // @handle_state = 1

    uv_close((uv_handle_t*)&fs_poll, dummy_close_cb);             // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 4: fs_poll_getpath on active handle
void variant_fs_poll_getpath() {
    uv_loop_t loop;
    uv_fs_poll_t fs_poll;
    char buf[256];
    size_t len = sizeof(buf);

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_fs_poll_init(&loop, &fs_poll);                              // @handle_state = 1
    uv_fs_poll_start(&fs_poll, dummy_fs_poll_cb, "/tmp/test", 500); // @handle_state = 2

    uv_fs_poll_getpath(&fs_poll, buf, &len);                      // requires @handle_state != 0

    uv_fs_poll_stop(&fs_poll);                                     // @handle_state = 1
    uv_close((uv_handle_t*)&fs_poll, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 5: both fs_event and fs_poll in same loop
void variant_both_watchers() {
    uv_loop_t loop;
    uv_fs_event_t fs_event;
    uv_fs_poll_t fs_poll;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_fs_event_init(&loop, &fs_event);                            // @handle_state = 1
    uv_fs_poll_init(&loop, &fs_poll);                              // @handle_state = 1

    uv_fs_event_start(&fs_event, dummy_fs_event_cb, "/tmp", 0);   // @handle_state = 2
    uv_fs_poll_start(&fs_poll, dummy_fs_poll_cb, "/tmp/f", 1000); // @handle_state = 2

    uv_fs_event_stop(&fs_event);                                   // @handle_state = 1
    uv_fs_poll_stop(&fs_poll);                                     // @handle_state = 1

    uv_close((uv_handle_t*)&fs_event, dummy_close_cb);
    uv_close((uv_handle_t*)&fs_poll, dummy_close_cb);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_fs_event_lifecycle();
    variant_fs_event_getpath();
    variant_fs_poll_lifecycle();
    variant_fs_poll_getpath();
    variant_both_watchers();
    return 0;
}
