// CVE-2020-8252: Buffer overflow in libuv's uv__realpath (should FAIL to compile)
//
// CVE-2020-8252 involves a buffer overflow in libuv's uv__realpath implementation
// used by fs.realpath in Node.js. The vulnerability is triggered by filesystem
// operations that reach libuv internals in an invalid state. DTT catches this
// pattern because uv_fs_realpath requires loop.@loop_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2020-8252

#include <uv.h>

// Variant 1: uv_fs_realpath on a closed loop
void variant_realpath_closed_loop() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);            // @loop_state = 1
    uv_loop_close(&loop);           // @loop_state = 3

    // DTT ERROR: requires arg0.@loop_state == 1, but @loop_state == 3
    uv_fs_realpath(&loop, &req, "/tmp/exploit", 0);
}

int main() {
    variant_realpath_closed_loop();
    return 0;
}
