// s2n recv before negotiate -- out-of-order state (should FAIL to compile)
//
// Tests that DTT catches s2n_recv called before the TLS handshake completes.
// After s2n_connection_set_fd, @conn_state == 2 (configured), but s2n_recv
// requires @conn_state == 3 (negotiated). Reading before handshake would
// attempt to decrypt data without an established TLS session.

#include <s2n.h>

// Variant 1: recv immediately after set_fd -- handshake never performed
void variant_recv_before_negotiate() {
    struct s2n_connection *conn = s2n_connection_new(S2N_SERVER);  // @conn_state = 1
    s2n_blocked_status blocked;
    char buf[1024];

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 3, but @conn_state == 2
    (void)s2n_recv(conn, buf, sizeof(buf), &blocked);
}

int main() {
    variant_recv_before_negotiate();
    return 0;
}
