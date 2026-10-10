// s2n connection wipe without proper state -- state violation (should FAIL to compile)
//
// Tests that DTT catches s2n_send after s2n_connection_wipe. After wipe,
// @conn_state == 5, but s2n_send requires @conn_state == 3 (negotiated).
// Wiping a connection resets it for reuse; data transfer requires re-negotiation.

#include <s2n.h>

// Variant 1: send after wipe -- connection has been reset
void variant_send_after_wipe() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    if (!conn) return;
    s2n_blocked_status blocked;

    (void)s2n_connection_set_fd(conn, 0);                          // @conn_state = 2
    (void)s2n_negotiate(conn, &blocked);                           // @conn_state = 3
    (void)s2n_shutdown(conn, &blocked);                            // @conn_state = 4
    (void)s2n_connection_wipe(conn);                               // @conn_state = 5

    // DTT ERROR: requires arg0.@conn_state == 3 (required state is not established in enforce mode)
    (void)s2n_send(conn, "hello", 5, &blocked);  // dtt-error {{requires @conn_state}}
}

int main() {
    variant_send_after_wipe();
    return 0;
}
