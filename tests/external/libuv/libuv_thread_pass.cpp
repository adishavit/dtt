// libuv thread pool / work queue -- correct usage patterns (should COMPILE)
//
// Tests uv_queue_work and uv_cancel with proper loop state.

#include <uv.h>

void dummy_work_cb(uv_work_t*) {}
void dummy_after_work_cb(uv_work_t*, int) {}

// Variant 1: queue_work with proper loop state
void variant_queue_work() {
    uv_loop_t loop;
    uv_work_t work_req;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_queue_work(&loop, &work_req, dummy_work_cb, dummy_after_work_cb); // requires @loop_state == 1

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);                                         // @loop_state = 3
}

// Variant 2: queue_work then cancel
void variant_queue_work_cancel() {
    uv_loop_t loop;
    uv_work_t work_req;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_queue_work(&loop, &work_req, dummy_work_cb, dummy_after_work_cb);
    uv_cancel((uv_req_t*)&work_req);                              // requires _nonnull

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 3: multiple work items
void variant_multiple_work_items() {
    uv_loop_t loop;
    uv_work_t work1;
    uv_work_t work2;
    uv_work_t work3;

    uv_loop_init(&loop);                                           // @loop_state = 1

    uv_queue_work(&loop, &work1, dummy_work_cb, dummy_after_work_cb);
    uv_queue_work(&loop, &work2, dummy_work_cb, dummy_after_work_cb);
    uv_queue_work(&loop, &work3, dummy_work_cb, dummy_after_work_cb);

    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_queue_work();
    variant_queue_work_cancel();
    variant_multiple_work_items();
    return 0;
}
