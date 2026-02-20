// CVE GHSA-q4mv: send on a freshly created connection (should FAIL to compile)
//
// GHSA-q4mv concerns memory leaks when config resources are mismanaged.
// A related misuse pattern is attempting data transfer on a connection that
// has never been configured or negotiated. After s2n_connection_new,
// @conn_state == 1 (created), but s2n_send requires @conn_state == 3
// (negotiated).
//
// Reference: https://github.com/aws/s2n-tls/security/advisories/GHSA-q4mv

#include <s2n.h>

// Variant 1: send on a brand-new connection — no config, no fd, no handshake
void variant_send_on_new_conn() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    // DTT ERROR: requires arg0.@conn_state == 3, but @conn_state == 1
    (void)s2n_send(conn, "hello", 5, &blocked);
}

int main() {
    variant_send_on_new_conn();
    return 0;
}
