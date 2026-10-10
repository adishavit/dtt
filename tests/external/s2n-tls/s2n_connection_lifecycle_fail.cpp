// s2n connection lifecycle — use after free (should FAIL to compile)
//
// Tests that DTT catches data transfer on a freed connection. After
// s2n_connection_free, @conn_state == 0, but s2n_send requires
// @conn_state == 3.

#include <s2n.h>

// Variant 1: send after connection_free — connection already torn down
void variant_send_after_free() {
    struct s2n_connection *conn = s2n_connection_new(S2N_CLIENT);  // @conn_state = 1
    if (!conn) return;
    s2n_blocked_status blocked;

    (void)s2n_connection_free(conn);                               // ensures @conn_state = 0

    // DTT ERROR: requires arg0.@conn_state == 3 (required state is not established in enforce mode)
    (void)s2n_send(conn, "hello", 5, &blocked);  // dtt-error {{requires @conn_state}}
}

int main() {
    variant_send_after_free();
    return 0;
}
