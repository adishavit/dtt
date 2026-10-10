// CVE-2024-24806: Improper domain lookup in uv_getaddrinfo (should FAIL to compile)
//
// CVE-2024-24806 involves improper domain lookup that can lead to data exposure
// via hostname truncation in libuv's uv_getaddrinfo. Operations performed on a
// closed event loop allow invalid internal state. DTT catches this because
// uv_getaddrinfo requires loop.@loop_state == 1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2024-24806

#include <uv.h>

// Variant 1: uv_getaddrinfo on a closed loop
void variant_getaddrinfo_closed_loop() {
    uv_loop_t loop;
    uv_getaddrinfo_t resolver;

    uv_loop_init(&loop);            // @loop_state = 1
    uv_loop_close(&loop);           // @loop_state = 3

    // DTT ERROR: requires arg0.@loop_state == 1, but @loop_state == 3
    uv_getaddrinfo(&loop, &resolver, 0, "example.com", "80", 0);  // dtt-error {{requires @loop_state}}
}

int main() {
    variant_getaddrinfo_closed_loop();
    return 0;
}
