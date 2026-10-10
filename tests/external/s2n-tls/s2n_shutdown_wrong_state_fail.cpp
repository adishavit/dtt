// s2n shutdown in wrong state -- state machine violation (should FAIL to compile)
//
// Tests that DTT catches s2n_shutdown called on a connection that has not
// been negotiated. After s2n_connection_new, @conn_state == 1, but
// s2n_shutdown requires @conn_state == 3 (negotiated).

#include <s2n.h>

// Variant 1: shutdown on a freshly created connection -- no handshake
void variant_shutdown_before_negotiate() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    if (!conn) return;
    s2n_blocked_status blocked;

    // DTT ERROR: requires arg0.@conn_state == 3 (required state is not established in enforce mode)
    (void)s2n_shutdown(conn, &blocked);  // dtt-error {{requires @conn_state}}
}

int main() {
    variant_shutdown_before_negotiate();
    return 0;
}
