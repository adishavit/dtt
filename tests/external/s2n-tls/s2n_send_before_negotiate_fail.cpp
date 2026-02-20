// s2n send before negotiate — out-of-order state transition (should FAIL to compile)
//
// Tests that DTT catches s2n_send called before the TLS handshake completes.
// After s2n_connection_set_fd, @conn_state == 2 (configured), but s2n_send
// requires @conn_state == 3 (negotiated). Skipping the handshake would send
// plaintext data over an unencrypted channel.

#include <s2n.h>

// Variant 1: send immediately after set_fd — handshake never performed
void variant_send_before_negotiate() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // ensures @conn_state = 2

    // DTT ERROR: requires arg0.@conn_state == 3, but @conn_state == 2
    (void)s2n_send(conn, "hello", 5, &blocked);
}

int main() {
    variant_send_before_negotiate();
    return 0;
}
