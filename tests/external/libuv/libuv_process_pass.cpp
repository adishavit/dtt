// libuv process operations -- correct usage patterns (should COMPILE)
//
// Tests uv_spawn, uv_process_kill, uv_kill, uv_process_get_pid
// with proper state transitions.

#include <uv.h>

void dummy_close_cb(uv_handle_t*) {}
void dummy_exit_cb(uv_process_t*, int64_t, int) {}

// Variant 1: uv_spawn -> uv_process_kill -> uv_close
void variant_spawn_and_kill() {
    uv_loop_t loop;
    uv_process_t process;
    uv_process_options_t options;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_spawn(&loop, &process, &options);                           // @handle_state = 2

    uv_process_kill(&process, 15);                                 // requires @handle_state == 2
    uv_process_get_pid(&process);                                  // requires _nonnull

    uv_close((uv_handle_t*)&process, dummy_close_cb);             // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: uv_kill (no handle required, just pid + signal)
void variant_kill_by_pid() {
    uv_kill(12345, 9);                                             // no state requirements
}

// Variant 3: spawn with exit callback and full cleanup
void variant_spawn_full_lifecycle() {
    uv_loop_t loop;
    uv_process_t process;
    uv_process_options_t options;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_spawn(&loop, &process, &options);                           // @handle_state = 2
    uv_process_get_pid(&process);

    uv_close((uv_handle_t*)&process, dummy_close_cb);             // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_spawn_and_kill();
    variant_kill_by_pid();
    variant_spawn_full_lifecycle();
    return 0;
}
